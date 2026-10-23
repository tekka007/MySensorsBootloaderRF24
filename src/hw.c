#include <util/delay.h>
#include "bootloader.h"
#include "board.h"
#include "hw.h"

// Watchdog ------------------------------------------------------------------------------------------------------------

NOINLINE void wdt_configure(const uint8_t config) {
	WDTCSR = _BV(WDCE) | _BV(WDE);
	WDTCSR = config;
}

// SPI -----------------------------------------------------------------------------------------------------------------

void spi_init(void) {
	BOARD_PIN_INIT();

	// SPE = SPI enable, MSTR = master, SPR1:0 = clock divider (0,0=/4, 0,1=/16, 1,0=/64, 1,1=/128), SPI2X doubles the rate.
	// nRF24L01+ supports up to 10 MHz, every setting below yields 1 MHz (0.5 MHz for slow clocks).
	#if (F_CPU >= 16000000)
		SPCR = _BV(SPE) | _BV(MSTR) | _BV(SPR0);
	#elif (F_CPU >= 8000000)
		SPCR = _BV(SPE) | _BV(MSTR) | _BV(SPR0);
		SPSR = _BV(SPI2X);
	#elif (F_CPU >= 4000000)
		SPCR = _BV(SPE) | _BV(MSTR);
	#elif (F_CPU >= 2000000)
		SPCR = _BV(SPE) | _BV(MSTR) | _BV(SPR0);
		SPSR = _BV(SPI2X);
	#else
		SPCR = _BV(SPE) | _BV(MSTR);
		SPSR = _BV(SPI2X);
	#endif
}

uint8_t spi_transfer(const uint8_t value) {
	SPDR = value;
	while (!(SPSR & _BV(SPIF)));
	return SPDR;
}

// UART ----------------------------------------------------------------------------------------------------------------

#ifndef BAUD_RATE
	#if F_CPU >= 16000000L
		#define BAUD_RATE   115200L
	#elif F_CPU >= 8000000L
		#define BAUD_RATE   57600L
	#elif F_CPU >= 1000000L
		#define BAUD_RATE   9600L   // 19200 also supported, but with significant error
	#elif F_CPU >= 128000L
		#define BAUD_RATE   4800L   // Good for 128kHz internal RC
	#else
		#define BAUD_RATE   1200L   // Good even at 32768Hz
	#endif
#endif

#define BAUD_SETTING	(((F_CPU + BAUD_RATE * 4L) / (BAUD_RATE * 8L)) - 1)
#define BAUD_ACTUAL		(F_CPU / (8 * (BAUD_SETTING + 1)))
#if BAUD_ACTUAL <= BAUD_RATE
	#define BAUD_ERROR	((100 * (BAUD_RATE - BAUD_ACTUAL)) / BAUD_RATE)
	#if BAUD_ERROR >= 5
		#error BAUD_RATE error greater than -5%
	#elif BAUD_ERROR >= 2
		#warning BAUD_RATE error greater than -2%
	#endif
#else
	#define BAUD_ERROR	((100 * (BAUD_ACTUAL - BAUD_RATE)) / BAUD_RATE)
	#if BAUD_ERROR >= 5
		#error BAUD_RATE error greater than 5%
	#elif BAUD_ERROR >= 2
		#warning BAUD_RATE error greater than 2%
	#endif
#endif

#if BAUD_SETTING > 250
	#error Unachievable baud rate (too slow) BAUD_RATE
#endif
#if BAUD_SETTING < 3
	#if BAUD_ERROR != 0 // permit high bitrates (ie 1Mbps@16MHz) if error is zero
		#error Unachievable baud rate (too fast) BAUD_RATE
	#endif
#endif

void uart_init(void) {
	UCSR0A = _BV(U2X0);	// double speed mode
	UCSR0B = _BV(RXEN0) | _BV(TXEN0);
	UCSR0C = _BV(UCSZ00) | _BV(UCSZ01);
	UBRR0L = (uint8_t)BAUD_SETTING;
}

void uart_putc(const uint8_t ch) {
	while (!(UCSR0A & _BV(UDRE0)));
	UDR0 = ch;
}

uint8_t uart_getc(void) {
	while (!(UCSR0A & _BV(RXC0)));
	// no framing error: the host is alive, keep the watchdog quiet
	if (!(UCSR0A & _BV(FE0))) {
		wdt_kick();
	}
	return UDR0;
}

// Flash ---------------------------------------------------------------------------------------------------------------

void flash_page_program(const uint16_t page) {
	__boot_page_erase_short(page);
	boot_spm_busy_wait();
	__boot_page_write_short(page);
	boot_spm_busy_wait();
	__boot_rww_enable_short();
}

static uint16_t crc16_update(uint16_t crc, const uint8_t data) {
	uint8_t count, mask_lo, mask_hi;
	__asm__ (
		"eor %A0, %4\n"
		"ldi %1, 8\n"
		"ldi %2, 0x01\n"
		"ldi %3, 0xA0\n"
		"1: lsr %B0\n"
		"ror %A0\n"
		"brcc 2f\n"
		"eor %B0, %3\n"
		"eor %A0, %2\n"
		"2: dec %1\n"
		"brne 1b\n"
		: "+r" (crc), "=&d" (count), "=&d" (mask_lo), "=&d" (mask_hi)
		: "r" (data));
	return crc;
}
uint16_t flash_crc(const uint16_t blocks) {
	const uint16_t len = blocks * FIRMWARE_BLOCK_SIZE;
	uint16_t crc = 0xFFFF;
	uint16_t address = 0x0000;
	// stop at the bootloader section, also prevents overflow
	while (address < len && address < BOOTLOADER_START_ADDRESS) {
		uint8_t rom_byte;
		// read a flash byte and increment the address
		__asm__ ("lpm %0,Z+\n" : "=r" (rom_byte), "=z" (address) : "1" (address));
		crc = crc16_update(crc, rom_byte);
	}
	return crc;
}

// EEPROM --------------------------------------------------------------------------------------------------------------
//
// Written in assembly: gcc turns these loops into about twice as many bytes. They are naked functions that follow the
// avr-gcc calling convention (arguments in r25:r24, r23:r22, r20; they only touch r0, r18 and X, never r1 or Y).
// Interrupts are never enabled in the bootloader, so the 4 cycle EEMPE/EEPE sequence needs no cli.
// EEPM = 00 (the reset value): a write erases and programs in one operation.

#define EE_WAIT_AND_SELECT \
	"1: sbic %[eecr], %[eepe]\n"		/* wait for a running write to finish */ \
	"rjmp 1b\n" \
	"out %[earh], r25\n" \
	"out %[earl], r24\n"

#define EE_OPERANDS \
	[eecr] "I" (_SFR_IO_ADDR(EECR)), [eedr] "I" (_SFR_IO_ADDR(EEDR)), \
	[earh] "I" (_SFR_IO_ADDR(EEARH)), [earl] "I" (_SFR_IO_ADDR(EEARL)), \
	[eere] "I" (EERE), [eepe] "I" (EEPE), [eempe] "I" (EEMPE)

// void eeprom_put(uint16_t address /* r25:r24 */, uint8_t value /* r22 */): only writes when the value differs
__attribute__((naked)) void eeprom_put(uint16_t address, uint8_t value) {
	__asm__ __volatile__ (
		EE_WAIT_AND_SELECT
		"sbi %[eecr], %[eere]\n"
		"in r0, %[eedr]\n"
		"cp r0, r22\n"
		"breq 2f\n"
		"out %[eedr], r22\n"
		"sbi %[eecr], %[eempe]\n"
		"sbi %[eecr], %[eepe]\n"
		"2: ret\n"
		:: EE_OPERANDS);
}

// void eeprom_put_block(uint16_t address /* r25:r24 */, const uint8_t *buf /* r23:r22 */, uint8_t len /* r20, > 0 */)
__attribute__((naked)) void eeprom_put_block(uint16_t address, const uint8_t *buf, uint8_t len) {
	__asm__ __volatile__ (
		"movw r26, r22\n"
		"1: ld r22, X+\n"
		"rcall eeprom_put\n"
		"adiw r24, 1\n"
		"dec r20\n"
		"brne 1b\n"
		"ret\n"
		::);
}

// void eeprom_get_block(uint16_t address /* r25:r24 */, void *buf /* r23:r22 */, uint8_t len /* r20, > 0 */)
__attribute__((naked)) void eeprom_get_block(uint16_t address, void *buf, uint8_t len) {
	__asm__ __volatile__ (
		"movw r26, r22\n"
		EE_WAIT_AND_SELECT
		"sbi %[eecr], %[eere]\n"
		"in r18, %[eedr]\n"
		"st X+, r18\n"
		"adiw r24, 1\n"
		"dec r20\n"
		"brne 1b\n"
		"ret\n"
		:: EE_OPERANDS);
}

// LED -----------------------------------------------------------------------------------------------------------------

void led_blink(void) {
	LED_DDR |= _BV(LED_PIN);
	// 300 ms total
	for (uint8_t i = 0; i < 6; i++) {
		LED_TOGGLE();
		_delay_ms(50);
	}
}
