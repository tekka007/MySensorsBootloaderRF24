/*
 * ATmega328p peripherals used by the bootloader: watchdog, SPI, UART, flash programming, CRC and status LED.
 */

#ifndef HW_H
#define HW_H

#include <stdint.h>
#include <avr/io.h>
#include "boot.h"

// Watchdog timeouts
#define WATCHDOG_OFF    (0)
#define WATCHDOG_16MS   (_BV(WDE))
#define WATCHDOG_32MS   (_BV(WDP0) | _BV(WDE))
#define WATCHDOG_64MS   (_BV(WDP1) | _BV(WDE))
#define WATCHDOG_125MS  (_BV(WDP1) | _BV(WDP0) | _BV(WDE))
#define WATCHDOG_250MS  (_BV(WDP2) | _BV(WDE))
#define WATCHDOG_500MS  (_BV(WDP2) | _BV(WDP0) | _BV(WDE))
#define WATCHDOG_1S     (_BV(WDP2) | _BV(WDP1) | _BV(WDE))
#define WATCHDOG_2S     (_BV(WDP2) | _BV(WDP1) | _BV(WDP0) | _BV(WDE))
#define WATCHDOG_4S     (_BV(WDP3) | _BV(WDE))
#define WATCHDOG_8S     (_BV(WDP3) | _BV(WDP0) | _BV(WDE))

static inline void wdt_kick(void) {
	__asm__ __volatile__ ("wdr\n");
}

void wdt_configure(uint8_t config);

// SPI master towards the nRF24
void spi_init(void);
uint8_t spi_transfer(uint8_t value);
static inline void spi_close(void) {
	SPCR = 0;
}

// UART used by the STK500 serial loader
void uart_init(void);
void uart_putc(uint8_t ch);
uint8_t uart_getc(void);

// Flash. flash_buffer_fill() fills the SPM page buffer, flash_page_program() erases and writes a page.
static inline void flash_buffer_fill(uint16_t address, uint16_t data) {
	__boot_page_fill_short(address, data);
}
void flash_page_program(uint16_t page);
// CRC16 of the first len bytes of flash (never reads into the bootloader section)
uint16_t flash_crc(uint16_t len);

void led_blink(void);

#endif // HW_H
