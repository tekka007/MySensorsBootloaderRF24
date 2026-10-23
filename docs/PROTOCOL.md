# Writing your own controller

All initialization the bootloader does (finding parent / requesting nodeId on first start) uses the same packets as a
normal MySensors sketch. There is no need for the controller to distinguish between packets from the bootloader and
packets from normal sketch execution. The controller only needs to care about two additional request/response
communications. All communication is binary.

## FirmwareConfig

The bootloader sends a `RequestFirmwareConfig` packet to the gateway to request information about the firmware it should
execute:

```c
typedef struct {
  uint16_t type;
  uint16_t version;
  uint16_t blocks;
  uint16_t crc;
  uint16_t BLVersion;
} RequestFirmwareConfig;
```

The gateway (the controller) responds with a `NodeFirmwareConfig` including details about the firmware the sensor
should execute:

```c
typedef struct {
  uint16_t type;
  uint16_t version;
  uint16_t blocks;
  uint16_t crc;
} NodeFirmwareConfig;
```

## Firmware

The bootloader sends a `RequestFirmwareBlock` packet to the gateway to request a specific subset (block) of the
compiled firmware:

```c
typedef struct {
  uint16_t type;
  uint16_t version;
  uint16_t block;
} RequestFirmwareBlock;
```

The gateway (the controller) responds with a `ResponseFirmwareBlock` including the specific block of the compiled
firmware (`FIRMWARE_BLOCK_SIZE` is 16 bytes):

```c
typedef struct {
  uint16_t type;
  uint16_t version;
  uint16_t block;
  uint8_t data[FIRMWARE_BLOCK_SIZE];
} ResponseFirmwareBlock;
```

Blocks are requested counting backwards, from `blocks - 1` down to 0. A `NodeFirmwareConfig` with `blocks == 0`, or with
`blocks` reaching into the bootloader section (`blocks >= 0x780`), is not treated as an update.

## Bootloader commands

Instead of firmware details the controller can answer the firmware config request with a command. A command is a
`NodeFirmwareConfig` with `blocks == 0` and `crc == 0xDA7A`; `type` carries the command and `version` its data:

| `type` | Command | `version` |
|--------|---------|-----------|
| `0x01` | Clear the whole EEPROM | unused |
| `0x02` | Set the node id | new node id |
| `0x03` | Set the parent node id | new parent node id |

Commands can be left out of the image with `-DBOOTLOADER_COMMANDS=OFF` to save about 70 bytes.
