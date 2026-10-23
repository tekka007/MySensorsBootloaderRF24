/*
 * Minimal nRF24L01+ driver: static pipe 0, dynamic payloads, auto ACK.
 */

#ifndef RF24_H
#define RF24_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	RF24_PA_MIN = 0,
	RF24_PA_LOW,
	RF24_PA_HIGH,
	RF24_PA_MAX
} rf24_pa_t;

typedef enum {
	RF24_1MBPS = 0,
	RF24_2MBPS,
	RF24_250KBPS
} rf24_datarate_t;

// Configures channel, data rate, PA level and pipe 0. Returns false if the radio does not answer as expected.
bool rf24_init(void);
// Sets the auto retransmit delay/count (SETUP_RETR) used for subsequent transmissions.
void rf24_set_retries(uint8_t delay, uint8_t count);
// Sends buf to node recipient, then switches back to RX with own_id as pipe 0 address. True if the packet was ACKed.
bool rf24_write_message(uint8_t recipient, uint8_t own_id, uint8_t *buf, uint8_t len);
bool rf24_data_available(void);
// Reads the pending payload into buf and returns its length (0 for invalid payloads).
uint8_t rf24_read_message(uint8_t *buf);

#endif // RF24_H
