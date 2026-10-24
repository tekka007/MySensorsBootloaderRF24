/*
 * MySensors OTA firmware loader: finds a parent, requests a node id and the firmware config from the controller and
 * downloads new firmware block by block. Does not return: it either starts the application or loops.
 */

#ifndef MYS_LOADER_H
#define MYS_LOADER_H

#include <stdint.h>

void mys_loader_run(void) __attribute__((noreturn));
// Stores the nodeFirmwareConfig_t in the shared RAM struct (R->fw_config) in EEPROM, only writing bytes that changed
void mys_save_firmware_config(void);

#endif // MYS_LOADER_H
