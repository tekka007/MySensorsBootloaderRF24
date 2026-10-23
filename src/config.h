/*
 * MYSBootloader build configuration.
 *
 * Every setting below is a default that can be overridden on the compiler
 * command line (-DNAME=value). The CMake build exposes the common ones as
 * cache variables, see README.md.
 */

#ifndef MYS_CONFIG_H
#define MYS_CONFIG_H

// RF24 communication settings ******************************************************************************************
#ifndef RF24_CHANNEL
	#define RF24_CHANNEL		(110)			// RF channel for the sensor net, 0-127; default 76
#endif
#ifndef RF24_DATARATE
	#define RF24_DATARATE		RF24_250KBPS	// RF24_250KBPS, RF24_1MBPS or RF24_2MBPS
#endif
#ifndef RF24_PA_LEVEL
	#define RF24_PA_LEVEL		RF24_PA_MAX		// RF24_PA_MIN=-18dBm, RF24_PA_LOW=-12dBm, RF24_PA_HIGH=-6dBm, RF24_PA_MAX=0dBm
#endif
#define RF24_ADDR_WIDTH		(5)
#define RF24_BASE_RADIO_ID_TAIL	0xFC,0xE1,0xA8,0xA8	// network address; the first address byte is the node id
#define RF24_ARC			(15)			// Auto Retransmit Counts, see nRF24L01+ data sheet
#define RF24_ARD			(5)				// Auto Retransmit Delay, 5=1500us, see nRF24L01+ data sheet

// Options **************************************************************************************************************
// Define NO_WATCHDOG_ON_SKETCH_START / NO_BOOTLOADER_COMMANDS to switch these off.
#if !defined(NO_WATCHDOG_ON_SKETCH_START) && !defined(WATCHDOG_ON_SKETCH_START)
	#define WATCHDOG_ON_SKETCH_START		// WDT on when application starts
#endif
#ifndef WDT_TIMEOUT
	#define WDT_TIMEOUT		WATCHDOG_8S		// WDT timeout
#endif
#if !defined(NO_BOOTLOADER_COMMANDS) && !defined(BOOTLOADER_COMMANDS)
	#define BOOTLOADER_COMMANDS				// Enable low level bootloader commands
#endif

// SPI bus setting ******************************************************************************************************
// Define exactly one SPI_PINS_* symbol matching your board's nRF24 wiring:
//   SPI_PINS_CE9_CSN10   default MySensors
//   SPI_PINS_CSN7_CE8
//   SPI_PINS_CE7_CSN8    RF-Nano Red
//   SPI_PINS_CE7_CSN10   NRF24Duino
//   SPI_PINS_CE4_CSN10
//   SPI_PINS_CE10_CSN9   RF-Nano Black
//   SPI_PINS_CE4_CSN5    CE=D4, CSN=D5 (e.g. RFduino, some RF-Nano variants)
#if !defined(SPI_PINS_CE9_CSN10) && !defined(SPI_PINS_CSN7_CE8) && !defined(SPI_PINS_CE7_CSN8) && \
	!defined(SPI_PINS_CE7_CSN10) && !defined(SPI_PINS_CE4_CSN10) && !defined(SPI_PINS_CE10_CSN9) && \
	!defined(SPI_PINS_CE4_CSN5)
	#define SPI_PINS_CE9_CSN10
#endif

// LED settings *********************************************************************************************************
#ifndef LED_DDR
	#define LED_DDR		DDRB
#endif
#ifndef LED_PORT
	#define LED_PORT	PORTB
#endif
// Writing a 1 to a PINx bit toggles the matching PORTx bit (ATmega328p), which is a single instruction
#define LED_TOGGLE()	(_SFR_IO8(_SFR_IO_ADDR(LED_PORT) - 2) = _BV(LED_PIN))
#ifndef LED_PIN
	#define LED_PIN		PINB5				// NRFDuino uses pin 9 (PB1): build with -DLED_PIN=PINB1
#endif

// DEBUG settings *******************************************************************************************************
// Define DEBUG to show the bootloader state as a bit pattern on DEBUG_PORT.
#ifndef DEBUG_PORT
	#define DEBUG_PORT	PORTD
#endif
#ifndef DEBUG_DDR
	#define DEBUG_DDR	DDRD
#endif

#endif // MYS_CONFIG_H
