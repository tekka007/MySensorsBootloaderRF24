/*
 * Runs a bootloader ELF in simavr against an emulated nRF24L01+ and MySensors gateway/controller.
 *
 *   [FREQ=<Hz>] [LIMIT_S=<simulated seconds>] harness <elf> <scenario> [out_prefix]
 *
 * Scenarios:
 *   ota        fresh node: find parent, get node id, download a 1 KiB firmware, verify CRC, start it
 *   rerun      node already holds the firmware (EEPROM + flash preloaded): must start it without downloading
 *   badcrc     controller announces a CRC the firmware does not match: node must not start the application
 *   cmd_clear  controller sends the "clear EEPROM" bootloader command
 *   cmd_id     controller sends the "set node id" command
 *   zero       controller announces blocks == 0: node must not request any block
 *   stk        serial STK500 session as avrdude does it (write 2 pages, read back, leave programming mode)
 *
 * Prints a summary and writes <out_prefix>.log (radio transcript) so two builds can be diffed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sim_avr.h"
#include "sim_elf.h"
#include "sim_core.h"
#include "avr_ioport.h"
#include "avr_spi.h"
#include "avr_uart.h"
#include "avr_eeprom.h"

#define EE_FW 0x11B			/* EEPROM_FIRMWARE_TYPE_ADDRESS */
#define IMG_BLOCKS 64

static avr_t *avr;
static FILE *logf_;
static const char *scenario;

/* ---- nRF24 model ---------------------------------------------------------------------------------------------- */
static uint8_t regs[0x20];
static int csn_active, idx;
static uint8_t cmd, rxbuf[32], txbuf[32];
static int txlen;
static uint8_t queue[8][32];
static int qlen[8], qn;
static uint8_t pending[32]; static int pending_len;
static int ce, ce_pending_rx;
static avr_irq_t *spi_in;
static uint8_t image[IMG_BLOCKS * 16];
static uint16_t image_crc;
static int blocks_requested, tx_count;

static uint16_t crc16(const uint8_t *p, int n) {
	uint16_t c = 0xFFFF;
	while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (c >> 1) ^ ((c & 1) ? 0xA001 : 0); }
	return c;
}

static void logline(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void logline(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt); vfprintf(logf_, fmt, ap); va_end(ap); fputc('\n', logf_);
}

static void hexdump(const char *tag, const uint8_t *p, int n) {
	fprintf(logf_, "%s", tag);
	for (int i = 0; i < n; i++) fprintf(logf_, " %02x", p[i]);
	fputc('\n', logf_);
}

static int build(uint8_t *out, uint8_t dest, uint8_t cap, uint8_t type, const uint8_t *pl, int len) {
	out[0] = 0; out[1] = 0; out[2] = dest; out[3] = (len << 3) | 2; out[4] = cap; out[5] = type; out[6] = 0xFF;
	memcpy(out + 7, pl, len);
	return 7 + len;
}

static void set_pending(uint8_t dest, uint8_t cap, uint8_t type, const uint8_t *pl, int len) {
	pending_len = build(pending, dest, cap, type, pl, len);
}

static void u16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

/* the controller: reacts on a packet transmitted by the node */
static void controller(const uint8_t *m, int n) {
	if (n < 7) return;
	uint8_t sender = m[1], command = m[4] & 7, type = m[5], len = m[3] >> 3;
	const uint8_t *pl = m + 7;
	uint8_t b[32];
	if (command == 3 && type == 7) {			/* I_FIND_PARENT_REQUEST */
		b[0] = 0; set_pending(sender, 0x23, 8, b, 1);
	} else if (command == 3 && type == 3) {		/* I_ID_REQUEST */
		b[0] = '5'; set_pending(sender, 0x03, 4, b, 1);
	} else if (command == 3 && type == 15) {	/* I_SIGNING_PRESENTATION */
		b[0] = 1; b[1] = 0; set_pending(sender, 0xC3, 15, b, 2);
	} else if (command == 4 && type == 0 && len == 10) {	/* ST_FIRMWARE_CONFIG_REQUEST */
		uint16_t t = 1, v = 2, blocks = IMG_BLOCKS, crc = image_crc;
		if (!strcmp(scenario, "badcrc")) crc ^= 0x1234;
		if (!strcmp(scenario, "zero")) { blocks = 0; crc = 0; }
		if (!strcmp(scenario, "cmd_clear")) { t = 1; v = 0; blocks = 0; crc = 0xDA7A; }
		if (!strcmp(scenario, "cmd_id")) { t = 2; v = 42; blocks = 0; crc = 0xDA7A; }
		u16(b, t); u16(b + 2, v); u16(b + 4, blocks); u16(b + 6, crc);
		set_pending(sender, 0xC4, 1, b, 8);
	} else if (command == 4 && type == 2 && len == 6) {	/* ST_FIRMWARE_REQUEST */
		int block = pl[4] | pl[5] << 8;
		blocks_requested++;
		memcpy(b, pl, 6);
		if (block < IMG_BLOCKS) memcpy(b + 6, image + block * 16, 16); else memset(b + 6, 0xEE, 16);
		set_pending(sender, 0xC4, 3, b, 22);
	} else {
		pending_len = 0;
	}
}

static void spi_out(struct avr_irq_t *irq, uint32_t value, void *param) {
	uint8_t mosi = value, miso = regs[7] | 0x0E;
	if (!csn_active) { avr_raise_irq(spi_in, 0xFF); return; }
	if (idx == 0) {
		cmd = mosi;
		logline("CMD %02x", cmd);
		if (cmd == 0x61 || cmd == 0x60) {
			if (qn) { memcpy(rxbuf, queue[0], qlen[0]); }
		}
		if (cmd == 0xE2) { qn = 0; }
		miso = (regs[7] & 0x70) | 0x0E;
	} else {
		int i = idx - 1;
		logline("  D %02x", mosi);
		if (cmd < 0x20) {							/* R_REGISTER */
			int r = cmd & 0x1F;
			if (r == 0x17) miso = (qn ? 0x10 : 0x11);
			else if (r == 0x07) miso = regs[7];
			else miso = regs[r];
		} else if (cmd < 0x40) {					/* W_REGISTER */
			int r = cmd & 0x1F;
			if (r == 0x07) regs[7] &= ~(mosi & 0x70); else if (i == 0) regs[r] = mosi;
			miso = 0;
		} else if (cmd == 0x60) {
			miso = qn ? qlen[0] : 0;
		} else if (cmd == 0x61) {
			miso = rxbuf[i];
		} else if (cmd == 0xA0) {
			txbuf[i] = mosi; txlen = i + 1; miso = 0;
		} else miso = 0;
	}
	idx++;
	avr_raise_irq(spi_in, miso);
}

static void csn_changed(struct avr_irq_t *irq, uint32_t value, void *param) {
	if (!value) { csn_active = 1; idx = 0; txlen = 0; }
	else {
		if (csn_active && cmd == 0x61 && qn) {		/* payload consumed */
			memmove(queue[0], queue[1], sizeof(queue[0]) * 7); memmove(qlen, qlen + 1, sizeof(int) * 7); qn--;
		}
		csn_active = 0;
	}
}

static void ce_changed(struct avr_irq_t *irq, uint32_t value, void *param) {
	if (value && !ce) {
		if (!(regs[0] & 1)) {						/* TX mode: packet goes out */
			tx_count++;
			hexdump("TX", txbuf, txlen);
			controller(txbuf, txlen);
			regs[7] |= 0x20;
			ce_pending_rx = pending_len;
		} else if (ce_pending_rx) {					/* switched to RX: reply arrives */
			memcpy(queue[qn], pending, pending_len); qlen[qn] = pending_len; qn++;
			hexdump("RX", pending, pending_len);
			ce_pending_rx = 0; pending_len = 0;
		}
	}
	ce = value;
}

/* ---- STK500 -------------------------------------------------------------------------------------------------- */
static uint8_t uart_out[4096]; static int uart_n;
static void uart_tx(struct avr_irq_t *irq, uint32_t value, void *param) { if (uart_n < (int)sizeof uart_out) uart_out[uart_n++] = value; }

static void run_cycles(uint64_t cycles) {
	uint64_t end = avr->cycle + cycles;
	while (avr->cycle < end && avr->state != cpu_Done && avr->state != cpu_Crashed) avr_run(avr);
}

static void uart_send(avr_irq_t *in, const uint8_t *p, int n) {
	for (int i = 0; i < n; i++) { avr_raise_irq(in, p[i]); run_cycles(avr->frequency / 200); }
}

static int run_stk(void) {
	avr_irq_t *in = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
	avr_irq_t *out = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT);
	avr_irq_register_notify(out, uart_tx, NULL);
	run_cycles(avr->frequency / 2);
	uart_n = 0;
	const uint8_t sync[] = { 0x30, 0x20 };
	uart_send(in, sync, 2); run_cycles(avr->frequency / 50);
	const uint8_t par[] = { 0x41, 0x82, 0x20, 0x41, 0x81, 0x20, 0x41, 0x99, 0x20 };
	uart_send(in, par, sizeof par);
	const uint8_t sign[] = { 0x75, 0x20 };
	uart_send(in, sign, 2);
	uint8_t setdev[1 + 20 + 1] = { 0x42 }; setdev[21] = 0x20; uart_send(in, setdev, sizeof setdev);
	uint8_t setext[1 + 5 + 1] = { 0x45 }; setext[6] = 0x20; uart_send(in, setext, sizeof setext);
	const uint8_t univ[] = { 0x56, 1, 2, 3, 4, 0x20 };
	uart_send(in, univ, sizeof univ);
	for (int page = 0; page < 2; page++) {
		uint16_t waddr = page * 64;
		uint8_t la[] = { 0x55, waddr & 0xFF, waddr >> 8, 0x20 };
		uart_send(in, la, sizeof la);
		uint8_t prog[4 + 128 + 1] = { 0x64, 0, 128, 'F' };
		for (int i = 0; i < 128; i++) prog[4 + i] = (uint8_t)(i * 7 + page * 31 + 1);
		prog[132] = 0x20;
		uart_send(in, prog, sizeof prog);
	}
	uint8_t la0[] = { 0x55, 0, 0, 0x20 }; uart_send(in, la0, sizeof la0);
	const uint8_t rd[] = { 0x74, 0, 16, 'F', 0x20 };
	uart_send(in, rd, sizeof rd);
	const uint8_t leave[] = { 0x51, 0x20 };
	uart_send(in, leave, sizeof leave);
	run_cycles(avr->frequency / 2);	/* let the EEPROM writes finish */
	hexdump("UART", uart_out, uart_n);
	printf("uart:"); for (int i = 0; i < uart_n; i++) printf(" %02x", uart_out[i]); printf("\n");
	return 0;
}

int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "usage: %s elf scenario [out_prefix]\n", argv[0]); return 2; }
	scenario = argv[2];
	char path[512]; snprintf(path, sizeof path, "%s.log", argc > 3 ? argv[3] : "sim");
	logf_ = fopen(path, "w");
	for (int i = 0; i < (int)sizeof image; i++) image[i] = (uint8_t)((i * 131) ^ (i >> 3) ^ 0x5A);
	image_crc = crc16(image, sizeof image);

	elf_firmware_t f = { {0} };
	if (elf_read_firmware(argv[1], &f)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
	strcpy(f.mmcu, "atmega328p");
	f.frequency = getenv("FREQ") ? strtoul(getenv("FREQ"), NULL, 10) : 16000000;
	avr = avr_make_mcu_by_name("atmega328p");
	avr_init(avr);
	avr->frequency = f.frequency;
	avr_load_firmware(avr, &f);
	avr->reset_pc = 0x7800; avr->pc = 0x7800; avr->codeend = 0x7FFF;
	memset(avr->flash, 0xFF, 0x7800);
	memcpy(avr->flash + 0x7800, f.flash + f.flashbase, f.flashsize);	/* bootloader at its address */
	avr->data[0x54] = !strcmp(scenario, "stk") ? 2 : 1;		/* MCUSR: external reset / power on */

	uint8_t *eep = malloc(1024); memset(eep, 0xFF, 1024);
	if (!strcmp(scenario, "rerun")) {
		memcpy(avr->flash, image, sizeof image);
		eep[0] = 5; eep[1] = 0; eep[2] = 1;
		uint8_t cfg[8]; u16(cfg, 1); u16(cfg + 2, 2); u16(cfg + 4, IMG_BLOCKS); u16(cfg + 6, image_crc);
		memcpy(eep + EE_FW, cfg, 8);
	}
	if (!strcmp(scenario, "cmd_clear") || !strcmp(scenario, "cmd_id") || !strcmp(scenario, "zero") || !strcmp(scenario, "badcrc")) {
		eep[0] = 5; eep[1] = 0; eep[2] = 1;
	}
	avr_eeprom_desc_t desc = { .ee = eep, .offset = 0, .size = 1024 };
	avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &desc);

	spi_in = avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_INPUT);
	avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_OUTPUT), spi_out, NULL);
	avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 2), csn_changed, NULL);
	avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 1), ce_changed, NULL);

	if (!strcmp(scenario, "stk")) {
		run_stk();
	} else {
		/* the bootloader starts the application by jumping to 0 */
		uint64_t limit = (uint64_t)avr->frequency * (getenv("LIMIT_S") ? strtoul(getenv("LIMIT_S"), NULL, 10) : 200);
		while (avr->cycle < limit && avr->state != cpu_Done && avr->state != cpu_Crashed) {
			avr_run(avr);
			if (avr->pc == 0 && avr->cycle > 1000) break;
		}
	}
	int started = (avr->pc == 0 && avr->cycle > 1000);
	avr_eeprom_desc_t got = { .ee = NULL, .offset = 0, .size = 1024 };
	avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &got);
	const uint8_t *e = got.ee;
	printf("scenario=%s started=%d cycles=%llu tx=%d blocks_requested=%d r2=%02x\n", scenario, started,
	       (unsigned long long)avr->cycle, tx_count, blocks_requested, avr->data[2]);
	printf("flash_crc_1k=%04x image_crc=%04x\n", crc16(avr->flash, sizeof image), image_crc);
	printf("eeprom[0..2]=%02x %02x %02x  fwcfg=", e[0], e[1], e[2]);
	for (int i = 0; i < 8; i++) printf("%02x ", e[EE_FW + i]);
	printf(" ee_ff_count=%d\n", ({ int n = 0; for (int i = 0; i < 1024; i++) n += e[i] == 0xFF; n; }));
	printf("flash_page0=");
	for (int i = 0; i < 16; i++) printf("%02x", avr->flash[i]);
	printf("\n");
	return 0;
}
