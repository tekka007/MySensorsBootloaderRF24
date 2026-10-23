#include <stddef.h>
#include <util/delay.h>
#include "bootloader.h"
#include "board.h"
#include "hw.h"
#include "rf24.h"
#include "rf24_registers.h"

#define RF24_SETUP		(((((RF24_DATARATE) & 0x02) << 4) | (((RF24_DATARATE) & 0x01) << 3) | ((RF24_PA_LEVEL) << 1)) + 1)
#define RF24_CONFIG		(_BV(CRCO) | _BV(EN_CRC))

// Pipe address: byte 0 is the node id, the remaining bytes are the constant network prefix set in rf24_init().
// (A PROGMEM table would be placed before main at the reset vector, so the prefix is stored with immediate values.)
static uint8_t base_addr[RF24_ADDR_WIDTH] NOINIT;

// Sends cmd followed by len bytes. In read mode the bytes are clocked in (sending NOP) and stored in buf, otherwise they
// are taken from buf. Returns the last byte received, i.e. the status for a command and the data for a one byte read.
static uint8_t spi_bytes(const uint8_t cmd, uint8_t *buf, uint8_t len, const bool read_mode) {
	CSN_LOW();
	_delay_us(10);
	uint8_t status = spi_transfer(cmd);
	while (len--) {
		status = spi_transfer(read_mode ? NOP : *buf);
		if (read_mode) *buf = status;
		buf++;
	}
	CSN_HIGH();
	_delay_us(10);
	return status;
}

static uint8_t write_byte(const uint8_t cmd, uint8_t value) {
	return spi_bytes(cmd, &value, 1, false);
}

NOINLINE static uint8_t read_byte(const uint8_t cmd) {
	uint8_t value;
	return spi_bytes(cmd, &value, 1, true);
}

static inline void write_register(const uint8_t reg, const uint8_t value) {
	(void)write_byte(reg | W_REGISTER, value);
}

static inline uint8_t read_register(const uint8_t reg) {
	return read_byte(reg | R_REGISTER);
}

static inline uint8_t command(const uint8_t cmd) {
	return spi_bytes(cmd, NULL, 0, false);
}

static void flush_and_clear(void) {
	(void)command(FLUSH_RX);
	(void)command(FLUSH_TX);
	write_register(STATUS, _BV(RX_DR) | _BV(TX_DS) | _BV(MAX_RT));
}

static bool write_payload(uint8_t *buf, const uint8_t len) {
	uint8_t status;
	(void)spi_bytes(W_TX_PAYLOAD, buf, len, false);
	// CE pulse starts the transmission
	CE_HIGH();
	_delay_us(4);
	do {
		status = command(NOP);
	} while (!(status & (_BV(TX_DS) | _BV(MAX_RT))));
	CE_LOW();
	flush_and_clear();
	return status & _BV(TX_DS);
}

NOINLINE static void set_pipe_address(const uint8_t pipe) {
	(void)spi_bytes(pipe | W_REGISTER, base_addr, RF24_ADDR_WIDTH, false);
}

bool rf24_data_available(void) {
	return !(read_register(FIFO_STATUS) & _BV(RX_EMPTY));
}

uint8_t rf24_read_message(uint8_t *buf) {
	uint8_t len = read_byte(R_RX_PL_WID);
	if (len > 32) {
		len = 0;
	}
	(void)spi_bytes(R_RX_PAYLOAD, buf, len, true);
	flush_and_clear();
	return len;
}

void rf24_set_retries(const uint8_t delay, const uint8_t count) {
	write_register(SETUP_RETR, delay << ARD | count << ARC);
}

bool rf24_write_message(const uint8_t recipient, const uint8_t own_id, uint8_t *buf, const uint8_t len) {
	// stop listening, switch to TX mode
	CE_LOW();
	write_register(CONFIG, RF24_CONFIG | _BV(PWR_UP));
	// pipe 0 RX/TX address is the recipient
	base_addr[0] = recipient;
	set_pipe_address(RX_ADDR_P0);
	set_pipe_address(TX_ADDR);
	const bool result = write_payload(buf, len);
	// back to listening, pipe 0 RX address is our own node id
	write_register(CONFIG, RF24_CONFIG | _BV(PWR_UP) | _BV(PRIM_RX));
	write_register(RX_ADDR_P0, own_id);
	CE_HIGH();
	return result;
}

bool rf24_init(void) {
	{
		static const uint8_t tail[] = { RF24_BASE_RADIO_ID_TAIL };
		for (uint8_t i = 0; i < sizeof(tail); i++) base_addr[i + 1] = tail[i];
	}
	CE_LOW();
	CSN_HIGH();
	write_register(SETUP_AW, RF24_ADDR_WIDTH - 2);
	write_register(RF_CH, RF24_CHANNEL);
	// data rate and PA level, +1 for Si24R1
	write_register(RF_SETUP, RF24_SETUP);
	// unlock features (certain nRF24 clones and non-P)
	(void)write_byte(ACTIVATE, 0x73);
	// dynamic payload length, pipe 0 only, auto ACK on pipe 0
	write_register(FEATURE, _BV(EN_DPL));
	write_register(EN_RXADDR, _BV(ERX_P0));
	write_register(EN_AA, _BV(ENAA_P0));
	write_register(DYNPD, _BV(DPL_P0));
	// sanity check
	return read_register(RF_SETUP) == RF24_SETUP;
}
