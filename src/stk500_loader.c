/*
 * STK500 optiboot code:
 * - Copyright 2013-2015 by Bill Westfield
 * - modified from optiboot: https://github.com/Optiboot/optiboot
 */

#include <stdbool.h>
#include "bootloader.h"
#include "hw.h"
#include "mys_loader.h"
#include "ram.h"
#include "stk500_loader.h"
#include "stk500_protocol.h"

static void verify_space(void) {
	if (uart_getc() != CRC_EOP) {
		// no space (0x20) received, reboot through the watchdog
		wdt_configure(WATCHDOG_16MS);
		while (1) {}
	}
	uart_putc(STK_INSYNC);
}

static void skip_bytes(uint8_t count) {
	do {
		(void)uart_getc();
	} while (--count);
	verify_space();
}

NOINLINE void stk500_loader_run(void) {
	uint8_t ch;
	uint16_t address = 0;
	uint8_t length;
	uint16_t fw_len = 0;
	bool done = false;

	uart_init();

	while (!done) {
		wdt_kick();
		ch = uart_getc();

		if (ch == STK_GET_PARAMETER) {
			ch = uart_getc();
			verify_space();
			if (ch == 0x82) {
				uart_putc(MYSBOOTLOADER_MINVER);
			} else if (ch == 0x81) {
				// out-of-sequence versioning to prevent collisions, see optiboot source
				uart_putc(MYSBOOTLOADER_MAJVER + 100);
			} else {
				uart_putc(0x03);
			}
		} else if (ch == STK_SET_DEVICE) {
			skip_bytes(20);
		} else if (ch == STK_SET_DEVICE_EXT) {
			skip_bytes(5);
		} else if (ch == STK_LOAD_ADDRESS) {
			// convert from word address to byte address
			address = (uart_getc() | uart_getc() << 8) * 2;
			verify_space();
		} else if (ch == STK_UNIVERSAL) {
			// UNIVERSAL command is ignored
			skip_bytes(4);
			uart_putc(0x00);
		} else if (ch == STK_PROG_PAGE) {
			(void)uart_getc();
			length = uart_getc();
			(void)uart_getc();
			fw_len += length;
			for (uint8_t i = 0; i < length; i += 2) {
				const uint16_t data = uart_getc() | (uart_getc() << 8);
				flash_buffer_fill(address + i, data);
			}
			flash_page_program(address);
			verify_space();
		} else if (ch == STK_READ_PAGE) {
			// flash only
			(void)uart_getc();
			length = uart_getc();
			(void)uart_getc();
			verify_space();
			do {
				// read a flash byte and increment the address
				__asm__ ("lpm %0,Z+\n" : "=r" (ch), "=z" (address) : "1" (address));
				uart_putc(ch);
			} while (--length);
		} else if (ch == STK_READ_SIGN) {
			verify_space();
			uart_putc(SIGNATURE_0);
			uart_putc(SIGNATURE_1);
			uart_putc(SIGNATURE_2);
		} else if (ch == STK_LEAVE_PROGMODE) {
			// type and version are not transferred by STK500, store 0xFFFF
			R->fw_config.type_command.type = 0xFFFF;
			R->fw_config.version_data.version = 0xFFFF;
			R->fw_config.blocks = fw_len / FIRMWARE_BLOCK_SIZE;
			R->fw_config.crc = flash_crc(R->fw_config.blocks);
			mys_save_firmware_config((const uint8_t *)&R->fw_config);
			done = true;
			verify_space();
		} else {
			verify_space();
		}
		uart_putc(STK_OK);
	}
}
