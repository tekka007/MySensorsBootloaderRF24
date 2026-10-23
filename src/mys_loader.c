#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <util/delay.h>
#include "bootloader.h"
#include "hw.h"
#include "mys_eeprom.h"
#include "mys_loader.h"
#include "mys_message.h"
#include "rf24.h"

#define TIMEOUT_MS						(2000)		// careful when changing
#define MAX_FIRMWARE_REQUEST_RESEND		(3)

#define SIGNING_PRESENTATION_VERSION_1	(1)
#define SIGNING_PRESENTATION_VALUE		(0)			// no signing

#define AUTO				(0xFFu)
#define NODE_SENSOR_ID		(0xFFu)
#define DISTANCE_INVALID	(0xFFu)
#define BROADCAST_ADDRESS	(0xFFu)
#define GATEWAY_ADDRESS		(0x00u)
#define DEBUG_NODE_ID		(0xFEu)

// Low level commands sent by the controller in a firmware config response with blocks == 0 and crc == BL_CMD_MAGIC
#define BL_CMD_MAGIC			(0xDA7Au)
#define BL_CMD_CLEAR_EEPROM		(0x01u)
#define BL_CMD_NODE_ID			(0x02u)
#define BL_CMD_PARENTNODE_ID	(0x03u)
#define EEPROM_ERASED_BYTE		(0xFFu)

// Pseudo commands used by process_rx(). Use out of range values
#define I_FIND_CONFIGURED_PARENT_RESPONSE_PSEUDO_COMMAND	(0xFE)
#define I_NO_ANSWER_PSEUDO_COMMAND							(0xFF)

typedef enum {
	BL_READ_CONFIG,
	BL_FIND_PARENTS,
	BL_CHECK_ID,
	BL_CONFIGURATION,
	BL_INIT_UPDATE,
	BL_DO_UPDATE,
	BL_RUN
} bl_state_t;

uint8_t mys_reset_flags NOINIT;

static MyMessage out_msg NOINIT;
static MyMessage in_msg NOINIT;
static nodeConfig_t node_config NOINIT;
static uint8_t configured_parent_id NOINIT;

void mys_save_firmware_config(const uint8_t *config) {
	eeprom_put_block(EEPROM_FIRMWARE_TYPE_ADDRESS, config, sizeof(nodeFirmwareConfig_t));
}

// message building ------------------------------------------------------------------------------------------------------

#define MSG_SIGN	(0)
#define REQ_ACK		(0)

// Fills the common header fields. Kept as a plain function with constant arguments (see build_message): inlining it
// at every call site costs more flash than the call.
static void build_message_proto(const uint8_t type, const uint8_t version_length, const uint8_t command_ack_payload) {
	out_msg.sender = node_config.nodeId;
	out_msg.last = node_config.nodeId;
	out_msg.sensor = NODE_SENSOR_ID;
	out_msg.type = type;
	out_msg.command_ack_payload = command_ack_payload;
	out_msg.version_length = version_length;
}

#define build_message(command, type, payload_type, length) \
	build_message_proto((type), (((length) << 3) | (MSG_SIGN << 2) | (PROTOCOL_VERSION & 3)), \
	                    (((payload_type) << 5) | (REQ_ACK << 3) | ((command) & 7)))

#define set_destination(dest)	(out_msg.destination = (dest))

static inline bool send_message(void) {
	return rf24_write_message(node_config.parentNodeId, node_config.nodeId, out_msg.array, HEADER_SIZE + mGetLength(out_msg));
}

// Reads one message, returns its type if it is addressed to us (or I_NO_ANSWER_PSEUDO_COMMAND).
// I_FIND_PARENT_RESPONSE messages update the parent/distance in node_config as a side effect.
static uint8_t process_rx(void) {
	uint8_t result = I_NO_ANSWER_PSEUDO_COMMAND;
	if (rf24_data_available()) {
		// discard faulty transmissions
		if (rf24_read_message(in_msg.array) != mGetLength(in_msg) + HEADER_SIZE) {
			return result;
		}
		if (in_msg.destination == node_config.nodeId) {
			result = in_msg.type;
			// parse internal find parent response only if configured parent not already found or not defined
			if ((mGetCommand(in_msg) == C_INTERNAL) && (in_msg.type == I_FIND_PARENT_RESPONSE) &&
			    ((node_config.parentNodeId != configured_parent_id) || (configured_parent_id == AUTO))) {
				if (in_msg.sender == configured_parent_id) {
					// configured parent found, save it
					node_config.parentNodeId = configured_parent_id;
					// stop the find parent waiting loop by answering using the pseudo command
					result = I_FIND_CONFIGURED_PARENT_RESPONSE_PSEUDO_COMMAND;
				}
				// payload value is sender distance to gateway, increase it to get our distance
				else if (in_msg.payload.bValue++ < node_config.distance) {
					node_config.distance = in_msg.payload.bValue;
					node_config.parentNodeId = in_msg.sender;
				}
			}
		}
	}
	return result;
}

// Send out_msg and process incoming messages until response_type arrives. Times out after TIMEOUT_MS per attempt and
// retries up to `retries` more times.
#if F_CPU == 1000000L
	#define COUNTER_START_VAL	(0xFFFE / 4)	// empirical value
#else
	#define COUNTER_START_VAL	(0xFFFE)
#endif
#define WAIT_DIVIDER	(COUNTER_START_VAL / TIMEOUT_MS)
#if F_CPU >= 16000000
	#define GRANULARITY_US	(200 / WAIT_DIVIDER)
#elif F_CPU >= 8000000
	#define GRANULARITY_US	(100 / WAIT_DIVIDER)
#elif F_CPU >= 2000000
	#define GRANULARITY_US	(25)
#else
	#define GRANULARITY_US	(0)
#endif

static bool send_process_type(const uint8_t response_type, uint8_t retries) {
	bool response = false;
	do {
		(void)send_message();
		uint16_t count = COUNTER_START_VAL;
		do {
			wdt_kick();
			response = (process_rx() == response_type);
			_delay_us(GRANULARITY_US);
		} while (count-- && !response);
	} while (retries-- && !response);
	return response;
}

// True if the two buffers differ (smaller than libc memcmp, which would be linked in just for this)
static bool differ(const uint8_t *a, const uint8_t *b, uint8_t len) {
	while (len--) {
		if (*a++ != *b++) return true;
	}
	return false;
}

// One byte atoi, smaller than the libc version
static uint8_t byte_atoi(const char *str, uint8_t len) {
	uint8_t result = 0;
	while (len--) {
		result = result * 10 + *str++ - '0';
	}
	return result;
}

// state machine ---------------------------------------------------------------------------------------------------------

void mys_loader_run(void) {
	nodeFirmwareConfig_t fw_config;
	uint16_t requested_block = 0u;
	uint16_t new_firmware_crc = 0xFFFF;
	// typed views on the message payloads
	requestFirmwareConfig_t *config_request = (requestFirmwareConfig_t *)out_msg.payload.data;
	nodeFirmwareConfig_t *config_response = (nodeFirmwareConfig_t *)in_msg.payload.data;
	requestFirmwareBlock_t *block_request = (requestFirmwareBlock_t *)out_msg.payload.data;
	responseFirmwareBlock_t *block_response = (responseFirmwareBlock_t *)in_msg.payload.data;
	bl_state_t state = BL_READ_CONFIG;

	spi_init();

	while (1) {
		switch (state) {
		case BL_READ_CONFIG:
			DEBUG_STATE(DEBUG_READ_CONFIG);
			// node id, parent node id, distance
			eeprom_get_block(EEPROM_NODE_ID_ADDRESS, &node_config, sizeof(nodeConfig_t));
			// firmware type, version, CRC, blocks
			eeprom_get_block(EEPROM_FIRMWARE_TYPE_ADDRESS, &fw_config, sizeof(nodeFirmwareConfig_t));
			// without a working radio proceed to the startup check
			state = rf24_init() ? BL_FIND_PARENTS : BL_RUN;
			break;

		case BL_FIND_PARENTS:
			DEBUG_STATE(DEBUG_FIND_PARENTS);
			// use the static parent if it answers, otherwise the closest node to the gateway
			configured_parent_id = node_config.parentNodeId;
			node_config.parentNodeId = AUTO;
			node_config.distance = DISTANCE_INVALID;
			set_destination(BROADCAST_ADDRESS);
			// ChinRF24 clones have an inverted NO_ACK bit: do not use the NO_ACK feature, but limit the retries to 3
			rf24_set_retries(5, 3);
			build_message(C_INTERNAL, I_FIND_PARENT_REQUEST, P_BYTE, 1);
			// wait for the pseudo command (not a real command), i.e. process messages until timeout or configured
			// parent found. Force 1 retry to ensure first message reception from routers (nRF24L01 PID problem).
			(void)send_process_type(I_FIND_CONFIGURED_PARENT_RESPONSE_PSEUDO_COMMAND, 1);
			// from now on all messages are directed to the gateway, auto retransmit delay 1500us, count 15
			set_destination(GATEWAY_ADDRESS);
			rf24_set_retries(RF24_ARD, RF24_ARC);
			// no reply (e.g. no parent nodes): proceed to startup
			state = (node_config.parentNodeId != AUTO) ? BL_CHECK_ID : BL_RUN;
			break;

		case BL_CHECK_ID:
			DEBUG_STATE(DEBUG_CHECK_ID);
			if (node_config.nodeId == GATEWAY_ADDRESS || node_config.nodeId == AUTO) {
				build_message(C_INTERNAL, I_ID_REQUEST, P_BYTE, 0);
				if (send_process_type(I_ID_RESPONSE, 3)) {
					#ifdef DEBUG
						// atoi would not leave room for the LED debug code
						const uint8_t new_id = DEBUG_NODE_ID;
					#else
						const uint8_t new_id = byte_atoi(in_msg.payload.data, mGetLength(in_msg));
					#endif
					eeprom_put(EEPROM_NODE_ID_ADDRESS, new_id);
					state = BL_READ_CONFIG;
				}
			} else {
				state = BL_CONFIGURATION;
			}
			break;

		case BL_CONFIGURATION:
			DEBUG_STATE(DEBUG_CONFIGURATION);
			// default: if no reply received, proceed to startup
			state = BL_RUN;
			// signing preferences: tell the gateway the bootloader does not require signed messages
			out_msg.payload.data[0] = SIGNING_PRESENTATION_VERSION_1;
			out_msg.payload.data[1] = SIGNING_PRESENTATION_VALUE;
			build_message(C_INTERNAL, I_SIGNING_PRESENTATION, P_CUSTOM, 2);
			(void)send_process_type(I_SIGNING_PRESENTATION, 0);
			// refresh the CRC in case of memory corruption or a failed transmission
			fw_config.crc = flash_crc(fw_config.blocks);
			(void)memcpy(config_request, &fw_config, sizeof(nodeFirmwareConfig_t));
			config_request->BLVersion = MYSBOOTLOADER_VERSION;
			build_message(C_STREAM, ST_FIRMWARE_CONFIG_REQUEST, P_CUSTOM, sizeof(requestFirmwareConfig_t));
			if (send_process_type(ST_FIRMWARE_CONFIG_RESPONSE, 3)) {
				DEBUG_STATE(DEBUG_CONFIG_RECEIVED);
				if (differ((const uint8_t *)&fw_config, (const uint8_t *)in_msg.payload.data, sizeof(nodeFirmwareConfig_t))) {
					#ifdef BOOTLOADER_COMMANDS
					if (!config_response->blocks && config_response->crc == BL_CMD_MAGIC) {
						const uint16_t command = config_response->type_command.bl_command;
						if (command == BL_CMD_CLEAR_EEPROM) {
							for (uint16_t i = 0; i < EEPROM_SIZE; i++) {
								eeprom_put(i, EEPROM_ERASED_BYTE);
							}
						} else if (command == BL_CMD_NODE_ID || command == BL_CMD_PARENTNODE_ID) {
							// 0x02 sets the node id, 0x03 the parent id (adjacent EEPROM bytes)
							eeprom_put(EEPROM_NODE_ID_ADDRESS + command - 2,
							                   (uint8_t)config_response->version_data.bl_data);
						}
						state = BL_READ_CONFIG;
					} else
					#endif
					{
						// An update needs at least one block, and must stay out of the bootloader section:
						// max block < BOOTLOADER_START_ADDRESS / FIRMWARE_BLOCK_SIZE. blocks == 0 would
						// request block 0xFFFF and wrap the flash address.
						if (config_response->blocks &&
						    config_response->blocks < (BOOTLOADER_START_ADDRESS / FIRMWARE_BLOCK_SIZE)) {
							(void)memcpy(&fw_config, in_msg.payload.data, sizeof(nodeFirmwareConfig_t));
							state = BL_INIT_UPDATE;
						}
					}
				}
			}
			break;

		case BL_INIT_UPDATE:
			DEBUG_STATE(DEBUG_INIT_UPDATE);
			state = BL_DO_UPDATE;
			new_firmware_crc = fw_config.crc;
			// invalidate the CRC and save the new details, so an interrupted transfer is detected on restart
			fw_config.crc = 0xFFFF;
			mys_save_firmware_config((const uint8_t *)&fw_config);
			requested_block = fw_config.blocks;
			break;

		case BL_DO_UPDATE:
			DEBUG_STATE(DEBUG_DO_UPDATE);
			block_request->type = fw_config.type_command.type;
			block_request->version = fw_config.version_data.version;
			block_request->block = requested_block - 1;
			build_message(C_STREAM, ST_FIRMWARE_REQUEST, P_CUSTOM, sizeof(requestFirmwareBlock_t));
			// the firmware is loaded counting backwards
			if (send_process_type(ST_FIRMWARE_RESPONSE, MAX_FIRMWARE_REQUEST_RESEND)) {
				if (!differ((const uint8_t *)block_request, (const uint8_t *)block_response, sizeof(requestFirmwareBlock_t))) {
					const uint8_t offset = ((requested_block - 1) * FIRMWARE_BLOCK_SIZE) % SPM_PAGESIZE;
					for (uint8_t i = 0; i < FIRMWARE_BLOCK_SIZE; i += 2) {
						flash_buffer_fill(offset + i, block_response->data[i] | block_response->data[i + 1] << 8);
					}
					// program the page once its first block (lowest address) arrived
					if (!offset) {
						flash_page_program((requested_block - 1) * FIRMWARE_BLOCK_SIZE);
					}
					requested_block--;
					if (!requested_block) {
						// last block received: validate the firmware CRC
						fw_config.crc = new_firmware_crc;
						if (flash_crc(fw_config.blocks) != new_firmware_crc) {
							// invalid firmware: 0 blocks, whose CRC evaluates to 0xFFFF
							fw_config.blocks = 0u;
							fw_config.crc = 0u;
						}
						mys_save_firmware_config((const uint8_t *)&fw_config);
						state = BL_READ_CONFIG;
					}
				}
			} else {
				state = BL_CONFIGURATION;
			}
			break;

		case BL_RUN:
			DEBUG_STATE(DEBUG_PREPARE_RUN);
			// verify the flash CRC against the one stored in EEPROM
			if (flash_crc(fw_config.blocks) == fw_config.crc) {
				DEBUG_STATE(DEBUG_RUN);
				spi_close();
				#ifdef WATCHDOG_ON_SKETCH_START
					wdt_kick();
				#else
					wdt_configure(WATCHDOG_OFF);
				#endif
				// hand the reset flags to the application in r2 (retrieved there using .init0)
				__asm__ __volatile__ ("mov r2, %0\n" :: "r" (mys_reset_flags));
				// run the application
				__asm__ __volatile__ ("jmp 0\n");
			}
			// CRC invalid, start over
			state = BL_READ_CONFIG;
			break;
		}
	}
}
