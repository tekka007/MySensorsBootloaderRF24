/*
 * MYSBootloader 1.4.0-rc.1
 * OTA RF24 bootloader for MySensors: https://www.mysensors.org
 * Based on MySensors library 2.2
 * Developed and maintained by tekka 2018
 *
 * Tested with MYSController https://www.mysensors.org/controller/myscontroller
 * MCU: ATmega328p
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * The bootloader must fit in the 2048 byte boot section, the linker places .text at 0x7800.
 * Build time options are documented in config.h and README.md, release history in CHANGELOG.md.
 */

#include <avr/io.h>
#include "bootloader.h"
#include "hw.h"
#include "mys_loader.h"
#include "ram.h"
#include "stk500_loader.h"

// The one instance of the bootloader RAM; R (the Y register) points at it for the whole run.
static bl_ram_t bl_ram NOINIT;

int main(void) __attribute__ ((OS_main, used, section (".init9")));

int main(void) {
	__asm__ volatile ("clr __zero_reg__");
	R = &bl_ram;
	// save the reset cause for the application, then clear it
	R->reset_flags = MCUSR;
	MCUSR = 0;
	wdt_configure(WDT_TIMEOUT);
	#ifdef DEBUG
		DEBUG_DDR = 0xFF;
		DEBUG_PORT = DEBUG_INIT;
	#endif
	// signal startup
	led_blink();
	// the serial STK500 loader runs only after an external reset or power on
	if (R->reset_flags & _BV(EXTRF)) {
		stk500_loader_run();
	}
	mys_loader_run();
}
