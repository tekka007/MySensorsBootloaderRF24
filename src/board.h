/*
 * nRF24 wiring per board. The SPI_PINS_* variant is chosen in config.h.
 * SCK/MISO/MOSI are the ATmega328p hardware SPI pins, CE and CSN are free to move.
 */

#ifndef BOARD_H
#define BOARD_H

#include <avr/io.h>

#define SPI_PORT	PORTB
#define SPI_DDR		DDRB
#define SPI_SCLK	PB5		// Arduino pin 13
#define SPI_MISO	PB4		// Arduino pin 12
#define SPI_MOSI	PB3		// Arduino pin 11

// BOARD_PIN_INIT() configures the data direction of every pin the radio uses.
// PB2 (SS) always has to be an output, otherwise the SPI peripheral drops into slave mode.
#if defined(SPI_PINS_CE9_CSN10)
	#define CSN_PORT	PORTB
	#define CSN_DDR		DDRB
	#define CSN_PIN		PB2		// Arduino pin 10
	#define CE_PORT		PORTB
	#define CE_DDR		DDRB
	#define CE_PIN		PB1		// Arduino pin 9
	#define BOARD_PIN_INIT()	(SPI_DDR = _BV(SPI_MOSI) | _BV(SPI_SCLK) | _BV(CE_PIN) | _BV(CSN_PIN))
#elif defined(SPI_PINS_CSN7_CE8)
	#define CSN_PORT	PORTD
	#define CSN_DDR		DDRD
	#define CSN_PIN		PD7		// Arduino pin 7
	#define CE_PORT		PORTB
	#define CE_DDR		DDRB
	#define CE_PIN		PB0		// Arduino pin 8
	#define BOARD_PIN_INIT()	do { \
			SPI_DDR = _BV(SPI_MOSI) | _BV(SPI_SCLK) | _BV(CE_PIN) | _BV(PB2); \
			CSN_DDR = _BV(CSN_PIN); \
		} while (0)
#elif defined(SPI_PINS_CE7_CSN8)
	// RF-Nano Red
	#define CE_PORT		PORTD
	#define CE_DDR		DDRD
	#define CE_PIN		PD7		// Arduino pin 7
	#define CSN_PORT	PORTB
	#define CSN_DDR		DDRB
	#define CSN_PIN		PB0		// Arduino pin 8
	#define BOARD_PIN_INIT()	do { \
			SPI_DDR = _BV(SPI_MOSI) | _BV(SPI_SCLK) | _BV(PB2); \
			CSN_DDR = _BV(CSN_PIN); \
			CE_DDR  = _BV(CE_PIN); \
		} while (0)
#elif defined(SPI_PINS_CE4_CSN10)
	#define CSN_PORT	PORTB
	#define CSN_DDR		DDRB
	#define CSN_PIN		PB2		// Arduino pin 10
	#define CE_PORT		PORTD
	#define CE_DDR		DDRD
	#define CE_PIN		PD4		// Arduino pin 4
	#define BOARD_PIN_INIT()	do { \
			SPI_DDR = _BV(SPI_MOSI) | _BV(SPI_SCLK) | _BV(PB2) | _BV(CSN_PIN); \
			CE_DDR = _BV(CE_PIN); \
		} while (0)
#elif defined(SPI_PINS_CE7_CSN10)
	// NRF24Duino, which has its LED on pin 9: build with -DLED_PIN=PINB1
	#define CE_PORT		PORTD
	#define CE_DDR		DDRD
	#define CE_PIN		PD7		// Arduino pin 7
	#define CSN_PORT	PORTB
	#define CSN_DDR		DDRB
	#define CSN_PIN		PB2		// Arduino pin 10
	#define BOARD_PIN_INIT()	do { \
			SPI_DDR = _BV(SPI_MOSI) | _BV(SPI_SCLK) | _BV(PB2) | _BV(CSN_PIN); \
			CE_DDR = _BV(CE_PIN); \
		} while (0)
#elif defined(SPI_PINS_CE10_CSN9)
	// RF-Nano Black
	#define CE_PORT		PORTB
	#define CE_DDR		DDRB
	#define CE_PIN		PB2		// Arduino pin 10
	#define CSN_PORT	PORTB
	#define CSN_DDR		DDRB
	#define CSN_PIN		PB1		// Arduino pin 9
	#define BOARD_PIN_INIT()	do { \
			SPI_DDR = _BV(SPI_MOSI) | _BV(SPI_SCLK) | _BV(PB2); \
			CSN_DDR = _BV(CSN_PIN); \
			CE_DDR  = _BV(CE_PIN); \
		} while (0)
#elif defined(SPI_PINS_CE4_CSN5)
	// RFduino and some RF-Nano variants: CE = D4 (PD4), CSN = D5 (PD5)
	#define CE_PORT		PORTD
	#define CE_DDR		DDRD
	#define CE_PIN		PD4
	#define CSN_PORT	PORTD
	#define CSN_DDR		DDRD
	#define CSN_PIN		PD5
	#define BOARD_PIN_INIT()	do { \
			SPI_DDR = _BV(SPI_MOSI) | _BV(SPI_SCLK) | _BV(PB2); \
			CE_DDR  = _BV(CE_PIN); \
			CSN_DDR = _BV(CSN_PIN); \
		} while (0)
#endif

#define CSN_LOW()	(CSN_PORT &= ~_BV(CSN_PIN))
#define CSN_HIGH()	(CSN_PORT |= _BV(CSN_PIN))
#define CE_LOW()	(CE_PORT &= ~_BV(CE_PIN))
#define CE_HIGH()	(CE_PORT |= _BV(CE_PIN))

#endif // BOARD_H
