/*
 * MySensors OTA firmware loader: finds a parent, requests a node id and the firmware config from the controller and
 * downloads new firmware block by block. Does not return: it either starts the application or loops.
 */

#ifndef MYS_LOADER_H
#define MYS_LOADER_H

#include <stdint.h>

extern uint8_t mys_reset_flags;		// MCUSR at reset, handed to the application in r2

void mys_loader_run(void) __attribute__((noreturn));
// Stores a nodeFirmwareConfig_t in EEPROM (only writes bytes that changed)
void mys_save_firmware_config(const uint8_t *config);

#endif // MYS_LOADER_H
