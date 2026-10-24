/*
 * All bootloader RAM in one struct, addressed through a pointer that lives permanently in the Y register (r28:r29).
 *
 * The AVR reaches memory at Y+0..Y+63 with one-word ldd/std instructions, while an absolute address needs the
 * two-word lds/sts. Putting the variables behind a reserved base pointer therefore saves one word per access, and the
 * frame setup of main() disappears as well. Y is reserved with -ffixed-r28 -ffixed-r29 (see CMakeLists.txt), which also
 * means the compiler cannot use a stack frame: nothing in the bootloader may need one. Fields are ordered so the
 * frequently used ones are within the first 64 bytes.
 *
 * main() points R at the struct. Nothing in it is initialised, every field is written before it is read (see NOINIT in bootloader.h).
 */

#ifndef RAM_H
#define RAM_H

#include "bootloader.h"
#include "mys_message.h"

typedef struct {
	nodeConfig_t node_config;
	uint8_t configured_parent_id;
	uint8_t scratch;					// one byte buffer for SPI transfers (there is no stack frame to take the address of)
	uint8_t reset_flags;				// MCUSR at reset, handed to the application in r2
	uint8_t base_addr[RF24_ADDR_WIDTH];	// nRF24 pipe address: node id followed by the network prefix
	nodeFirmwareConfig_t fw_config;
	uint16_t requested_block;			// firmware block being requested, counting down
	uint16_t new_firmware_crc;
	MyMessage out_msg;
} bl_ram_t;

register bl_ram_t *R asm("r28");

// The receive buffer does not fit in the 64 byte reach of Y together with everything else, so it keeps absolute addressing.
extern MyMessage in_msg;

#endif // RAM_H
