/*
 * MYSBootloader
 * OTA RF24 bootloader for MySensors: https://www.mysensors.org
 * Based on MySensors library 2.2
 * Developed and maintained by tekka 2018
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include "config.h"

// version
#define MYSBOOTLOADER_MAJVER 1
#define MYSBOOTLOADER_MINVER 3
#define MYSBOOTLOADER_VERSION ((MYSBOOTLOADER_MINVER << 8) + MYSBOOTLOADER_MAJVER)

// The bootloader lives in the top 2 KiB of the ATmega328p flash, the linker places .text at BOOTLOADER_START_ADDRESS.
#define BOOTLOADER_SIZE				(2048)
#define BOOTLOADER_START_ADDRESS	(0x8000 - BOOTLOADER_SIZE)

// Variables in .noinit are neither zeroed nor initialised, which removes the startup code (__do_clear_bss and
// __do_copy_data). Every variable placed there is written before it is read.
#define NOINIT __attribute__((section(".noinit")))

#define FIRMWARE_BLOCK_SIZE			(16)
#define EEPROM_SIZE					(1024)		// ATmega328p

// DEBUG led patterns. DEBUG_PORT is written with '=' (not '|=') so each state replaces the previous one.
// Values must fit in 8 bits (the port width).
#define DEBUG_INIT				_BV(3)
#define DEBUG_READ_CONFIG		_BV(4)
#define DEBUG_FIND_PARENTS		_BV(5)
#define DEBUG_CHECK_ID			_BV(6)
#define DEBUG_CONFIGURATION		_BV(7)
#define DEBUG_CONFIG_RECEIVED	_BV(5)
#define DEBUG_INIT_UPDATE		(_BV(6) | _BV(7))
#define DEBUG_DO_UPDATE			(_BV(4) | _BV(5) | _BV(7))
#define DEBUG_PREPARE_RUN		(_BV(4) | _BV(5) | _BV(7))
#define DEBUG_RUN				(_BV(4) | _BV(5) | _BV(6) | _BV(7))

#ifdef DEBUG
	#define DEBUG_STATE(state)	(DEBUG_PORT = (state))
#else
	#define DEBUG_STATE(state)	((void)0)
#endif

// OTA protocol structures (see README / docs/PROTOCOL.md)
#include <stdint.h>

typedef struct {
	union {
		uint16_t type;
		uint16_t bl_command;
	} type_command;
	union {
		uint16_t version;
		uint16_t bl_data;
	} version_data;
	uint16_t blocks;
	uint16_t crc;
} __attribute__((packed)) nodeFirmwareConfig_t;

typedef struct {
	uint16_t type;
	uint16_t version;
	uint16_t blocks;
	uint16_t crc;
	uint16_t BLVersion;
} __attribute__((packed)) requestFirmwareConfig_t;

typedef struct {
	uint16_t type;
	uint16_t version;
	uint16_t block;
} __attribute__((packed)) requestFirmwareBlock_t;

typedef struct {
	uint16_t type;
	uint16_t version;
	uint16_t block;
	uint8_t data[FIRMWARE_BLOCK_SIZE];
} __attribute__((packed)) responseFirmwareBlock_t;

typedef struct {
	uint8_t nodeId;			// Current node id
	uint8_t parentNodeId;	// Where this node sends its messages
	uint8_t distance;		// This nodes distance to sensor net gateway (number of hops)
} __attribute__((packed)) nodeConfig_t;

#endif // BOOTLOADER_H
