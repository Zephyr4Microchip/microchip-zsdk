/*
 * Copyright (c) 2026 Microchip Technology Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * MDFU (Microchip Device Firmware Update) client - UART transport
 *
 * Receives a signed firmware image from an MDFU host (e.g. the `pymdfu` tool)
 * over UART and writes it to MCUboot's secondary slot (slot1) using the
 * flash_img API.  Implements MDFU protocol version 1.3.0, serial transport.
 *
 * Transport frame:
 *   [0x56 START][ escaped( packet + checksum_LE16 ) ][0x9E END]
 *   Byte-stuffing (escape 0xCC): 0x56->CC A9, 0x9E->CC 61, 0xCC->CC 33.
 *   checksum = two's-complement 16-bit sum of the packet's LE16 words,
 *   bitwise-inverted (odd length is zero-padded before summing).
 *
 * Command packet (host->device): [seq_field][command][data...]
 * Status  packet (device->host): [seq_field][status ][data...]
 *   seq_field: bits 0-4 = sequence number (0..31),
 *              bit 7 = sync (command), bit 6 = resend (status).
 *
 * RX transport:
 *   Received bytes are captured by an interrupt-driven UART callback into a
 *   software ring buffer, and drained by the protocol loop.  This avoids the
 *   RX overrun that a polled read hits on a FIFO-less UART at 115200
 *   baud (a byte arrives every ~87 us).  TX uses polled uart_poll_out(), which
 *   cannot overrun.
 */

#include "uart_dfu.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/ring_buffer.h>
#include <string.h>

/* --- MDFU transport framing ---------------------------------------------- */

#define FRAME_START       0x56u
#define FRAME_END         0x9Eu
#define FRAME_ESC         0xCCu

/* --- MDFU protocol ------------------------------------------------------- */

#define MDFU_PROTO_MAJOR  1u
#define MDFU_PROTO_MINOR  3u
#define MDFU_PROTO_PATCH  0u

/* Commands (host -> device) */
#define CMD_GET_CLIENT_INFO  0x01u
#define CMD_START_TRANSFER   0x02u
#define CMD_WRITE_CHUNK      0x03u
#define CMD_GET_IMAGE_STATE  0x04u
#define CMD_END_TRANSFER     0x05u

/* Status codes (device -> host) */
#define STATUS_SUCCESS               0x01u
#define STATUS_COMMAND_NOT_SUPPORTED 0x02u
#define STATUS_NOT_AUTHORIZED        0x03u
#define STATUS_COMMAND_NOT_EXECUTED  0x04u
#define STATUS_ABORT_FILE_TRANSFER   0x05u

/* "Command not executed" cause bytes (status packet data) */
#define CAUSE_TRANSPORT_INTEGRITY 0x00u
#define CAUSE_COMMAND_TOO_LONG    0x01u
#define CAUSE_COMMAND_TOO_SHORT   0x02u
#define CAUSE_SEQUENCE_INVALID    0x03u

/* File-transfer abort cause bytes (status packet data) */
#define ABORT_GENERIC_CLIENT_ERROR 0x00u
#define ABORT_WRITE_ERROR          0x05u

/* Get Image State response byte */
#define IMAGE_STATE_VALID    0x01u
#define IMAGE_STATE_INVALID  0x02u

/* sequence field bit masks */
#define SEQ_MASK       0x1Fu
#define SEQ_SYNC_BIT   0x80u
#define SEQ_RESEND_BIT 0x40u

/* MDFU client-info parameter types */
#define CINFO_PROTOCOL_VERSION 0x01u
#define CINFO_BUFFER_INFO      0x02u
#define CINFO_COMMAND_TIMEOUTS 0x03u

/* Command-timeout units are 0.1 s per LSB.  Default 10 s => 100 LSB. */
#define DEFAULT_TIMEOUT_LSB  100u

/* Number of command buffers we advertise. */
#define BUFFER_COUNT  1u

/* Slot the upload targets (secondary slot). */
#define SLOT1_AREA_ID  PARTITION_ID(slot1_partition)

/* Per-byte read timeout once a frame is in progress (ms) */
#define BYTE_TIMEOUT_MS   2000
/* Timeout waiting for the first frame of a new command (ms) */
#define IDLE_TIMEOUT_MS   30000

/* Largest MDFU packet data we accept = advertised buffer size (WRITE_CHUNK). */
#define MDFU_MAX_DATA  UART_DFU_CHUNK_SIZE

/* --- Static buffers (keep off the stack) --------------------------------- */

static struct flash_img_context s_flash_ctx;

/*
 * A packet is [seq][cmd] + up to MDFU_MAX_DATA data bytes.
 * The received frame carries packet + 2 checksum bytes; s_packet holds the
 * decoded (unescaped) packet+checksum, s_data the outgoing status packet.
 */
static uint8_t s_packet[2u + MDFU_MAX_DATA + 2u];
static uint8_t s_status[2u + 64u];

/* Cache of the last executed command's reply, for duplicate retransmission. */
static uint8_t s_last[sizeof(s_status)];

/* --- Interrupt-driven RX ------------------------------------------------- */

/*
 * Bytes received by the UART ISR are buffered here and drained by recv_byte().
 * 512 B comfortably absorbs a full escaped WRITE_CHUNK frame plus framing
 * overhead between drains, well ahead of the ~87 us/byte arrival rate.
 */
#define RX_RING_SIZE 512u

static uint8_t rx_ring_buf[RX_RING_SIZE];
static struct ring_buf rx_rb;
static struct k_sem rx_sem;

/* UART RX interrupt handler: move received bytes into the ring buffer. */
static void uart_dfu_isr(const struct device *uart, void *user_data)
{
	ARG_UNUSED(user_data);
	uint8_t byte;
	bool got = false;

	if (!uart_irq_update(uart)) {
		return;
	}

	while (uart_irq_rx_ready(uart)) {
		if (uart_fifo_read(uart, &byte, 1) != 1) {
			break;
		}
		/* Drop bytes only if the consumer has fallen behind by 512 B,
		 * which should not happen at these rates; the protocol layer
		 * recovers via the checksum/resend path if it ever does. */
		(void)ring_buf_put(&rx_rb, &byte, 1);
		got = true;
	}

	if (got) {
		k_sem_give(&rx_sem);
	}
}

/* --- MDFU checksum -------------------------------------------------------- */

/*
 * Two's-complement 16-bit sum of little-endian 16-bit words, then inverted.
 * An odd number of bytes is treated as if padded with a trailing zero byte.
 */
static uint16_t mdfu_checksum(const uint8_t *data, size_t len)
{
	uint32_t sum = 0u;
	size_t i;

	for (i = 0u; (i + 1u) < len; i += 2u) {
		sum += (uint32_t)data[i] | ((uint32_t)data[i + 1u] << 8);
	}
	if (i < len) {
		/* trailing (odd) byte, high byte padded with zero */
		sum += (uint32_t)data[i];
	}
	return (uint16_t)(~sum & 0xFFFFu);
}

/* --- Low-level UART helpers ---------------------------------------------- */

/*
 * Pull one byte from the RX ring buffer, blocking up to timeout_ms for the ISR
 * to deliver it.  Blocking is safe here: the ISR keeps capturing bytes into the
 * ring buffer while we sleep, so no data is lost (unlike a polled read of the
 * FIFO-less UART RX register).
 */
static int recv_byte(uint8_t *byte, int32_t timeout_ms)
{
	uint32_t start = k_uptime_get_32();

	while (ring_buf_get(&rx_rb, byte, 1) == 0) {
		int32_t elapsed = (int32_t)(k_uptime_get_32() - start);

		if (elapsed >= timeout_ms) {
			return -ETIMEDOUT;
		}
		/* Wait for the ISR to signal new data (or the deadline). */
		(void)k_sem_take(&rx_sem, K_MSEC(timeout_ms - elapsed));
	}
	return 0;
}

static inline void send_byte(const struct device *uart, uint8_t b)
{
	uart_poll_out(uart, b);
}

/* Emit one payload byte, inserting an escape sequence for reserved codes. */
static void send_escaped(const struct device *uart, uint8_t b)
{
	switch (b) {
	case FRAME_START:
		send_byte(uart, FRAME_ESC);
		send_byte(uart, (uint8_t)~FRAME_START);
		break;
	case FRAME_END:
		send_byte(uart, FRAME_ESC);
		send_byte(uart, (uint8_t)~FRAME_END);
		break;
	case FRAME_ESC:
		send_byte(uart, FRAME_ESC);
		send_byte(uart, (uint8_t)~FRAME_ESC);
		break;
	default:
		send_byte(uart, b);
		break;
	}
}

/* --- MDFU transport: send a status packet as a framed transport frame ----- */

static void send_status(const struct device *uart, const uint8_t *packet, size_t len)
{
	uint16_t crc = mdfu_checksum(packet, len);

	send_byte(uart, FRAME_START);
	for (size_t i = 0u; i < len; i++) {
		send_escaped(uart, packet[i]);
	}
	send_escaped(uart, (uint8_t)(crc & 0xFFu));
	send_escaped(uart, (uint8_t)(crc >> 8));
	send_byte(uart, FRAME_END);
}

/*
 * Receive one transport frame and return the decoded packet (without the
 * trailing 2-byte checksum) in s_packet.
 *
 * Returns the packet length (>=0) on success, or:
 *   -ETIMEDOUT : no/incomplete frame within the timeout
 *   -EBADMSG   : checksum mismatch or malformed escape sequence
 *   -EMSGSIZE  : packet larger than s_packet
 *
 * The first byte is awaited using idle_timeout_ms; subsequent bytes use
 * BYTE_TIMEOUT_MS.
 */
static int recv_packet(uint16_t *packet_len, int32_t idle_timeout_ms)
{
	uint8_t byte;
	size_t n = 0u;
	bool escaped = false;

	/* Sync: discard until FRAME_START (tolerates console text before it). */
	do {
		if (recv_byte(&byte, idle_timeout_ms) < 0) {
			return -ETIMEDOUT;
		}
	} while (byte != FRAME_START);

	/* Read until FRAME_END, undoing byte-stuffing as we go. */
	for (;;) {
		if (recv_byte(&byte, BYTE_TIMEOUT_MS) < 0) {
			return -ETIMEDOUT;
		}

		if (!escaped) {
			if (byte == FRAME_END) {
				break;
			}
			if (byte == FRAME_START) {
				/* Unexpected re-sync: restart the frame. */
				n = 0u;
				continue;
			}
			if (byte == FRAME_ESC) {
				escaped = true;
				continue;
			}
		} else {
			if (byte == (uint8_t)~FRAME_START) {
				byte = FRAME_START;
			} else if (byte == (uint8_t)~FRAME_END) {
				byte = FRAME_END;
			} else if (byte == (uint8_t)~FRAME_ESC) {
				byte = FRAME_ESC;
			} else {
				return -EBADMSG; /* unknown escape sequence */
			}
			escaped = false;
		}

		if (n >= sizeof(s_packet)) {
			return -EMSGSIZE;
		}
		s_packet[n++] = byte;
	}

	if (escaped) {
		return -EBADMSG; /* frame ended mid-escape */
	}
	if (n < 2u) {
		return -EBADMSG; /* need at least the 2 checksum bytes */
	}

	/* Split off and verify the trailing 2-byte little-endian checksum. */
	size_t data_len = n - 2u;
	uint16_t crc_recv = (uint16_t)s_packet[data_len] |
			    ((uint16_t)s_packet[data_len + 1u] << 8);

	if (crc_recv != mdfu_checksum(s_packet, data_len)) {
		return -EBADMSG;
	}

	*packet_len = (uint16_t)data_len;
	return 0;
}

/* --- Status packet builders ---------------------------------------------- */

/* Build a status packet header (seq + status) and return the write cursor. */
static size_t status_begin(uint8_t seq, uint8_t status, bool resend)
{
	s_status[0] = (uint8_t)((seq & SEQ_MASK) | (resend ? SEQ_RESEND_BIT : 0u));
	s_status[1] = status;
	return 2u;
}

/* --- Firmware verification (Get Image State) ------------------------------ */

/*
 * Lightweight validity check of the image just written to slot1.
 *
 * Full cryptographic signature verification is performed by MCUboot on the
 * next boot (test-boot + automatic rollback on failure).  Here we only sanity
 * check that a plausible MCUboot image header landed in the slot.
 */
static bool slot1_image_looks_valid(void)
{
	struct mcuboot_img_header hdr;
	const struct flash_area *fa;
	size_t slot_size;
	int rc;

	rc = boot_read_bank_header(SLOT1_AREA_ID, &hdr, sizeof(hdr));
	if (rc != 0) {
		printk("MDFU: boot_read_bank_header failed: %d\n", rc);
		return false;
	}

	/* Header must describe a non-empty image. */
	if (hdr.h.v1.image_size == 0u) {
		printk("MDFU: image_size is 0\n");
		return false;
	}

	/* Image (header + body) must fit within the slot. */
	rc = flash_area_open(SLOT1_AREA_ID, &fa);
	if (rc != 0) {
		printk("MDFU: flash_area_open failed: %d\n", rc);
		return false;
	}
	slot_size = fa->fa_size;
	flash_area_close(fa);

	if ((size_t)hdr.h.v1.image_size > slot_size) {
		printk("MDFU: image_size %u > slot_size %u\n",
		       hdr.h.v1.image_size, (unsigned)slot_size);
		return false;
	}

	return true;
}

/* --- Command handlers ----------------------------------------------------- */

/* GET_CLIENT_INFO: return the TLV-encoded client information blob. */
static size_t handle_get_client_info(uint8_t seq)
{
	size_t p = status_begin(seq, STATUS_SUCCESS, false);

	/* BUFFER_INFO: buffer_size (LE16) + buffer_count (1) */
	s_status[p++] = CINFO_BUFFER_INFO;
	s_status[p++] = 3u;
	s_status[p++] = (uint8_t)(UART_DFU_CHUNK_SIZE & 0xFFu);
	s_status[p++] = (uint8_t)(UART_DFU_CHUNK_SIZE >> 8);
	s_status[p++] = BUFFER_COUNT;

	/* PROTOCOL_VERSION: major.minor.patch */
	s_status[p++] = CINFO_PROTOCOL_VERSION;
	s_status[p++] = 3u;
	s_status[p++] = MDFU_PROTO_MAJOR;
	s_status[p++] = MDFU_PROTO_MINOR;
	s_status[p++] = MDFU_PROTO_PATCH;

	/* COMMAND_TIMEOUTS: default timeout only (cmd id 0 + LE16 in 0.1s) */
	s_status[p++] = CINFO_COMMAND_TIMEOUTS;
	s_status[p++] = 3u;
	s_status[p++] = 0u; /* default timeout selector */
	s_status[p++] = (uint8_t)(DEFAULT_TIMEOUT_LSB & 0xFFu);
	s_status[p++] = (uint8_t)(DEFAULT_TIMEOUT_LSB >> 8);

	return p;
}

/* --- RX transport setup/teardown ----------------------------------------- */

static void rx_transport_start(const struct device *uart)
{
	/* Discard anything already latched, then arm interrupt-driven RX. */
	ring_buf_init(&rx_rb, sizeof(rx_ring_buf), rx_ring_buf);
	k_sem_init(&rx_sem, 0, 1);

	uart_irq_rx_disable(uart);
	uart_irq_tx_disable(uart);
	uart_irq_callback_user_data_set(uart, uart_dfu_isr, NULL);
	uart_irq_rx_enable(uart);
}

static void rx_transport_stop(const struct device *uart)
{
	uart_irq_rx_disable(uart);
}

/* --- Public API ----------------------------------------------------------- */

int uart_dfu_receive(const struct device *uart_dev)
{
	uint16_t packet_len;
	uint8_t expected_seq = 0u;
	uint8_t last_seq = 0u;
	size_t last_status_len = 0u;
	bool transfer_active = false;
	int rc;

	/* Target the secondary slot (slot1). */
	rc = flash_img_init(&s_flash_ctx);
	if (rc < 0) {
		return rc;
	}

	rx_transport_start(uart_dev);

	for (;;) {
		int32_t idle = transfer_active ? BYTE_TIMEOUT_MS : IDLE_TIMEOUT_MS;

		rc = recv_packet(&packet_len, idle);
		if (rc == -ETIMEDOUT) {
			rc = -ETIMEDOUT;
			goto out;
		}
		if (rc == -EBADMSG || rc == -EMSGSIZE) {
			/*
			 * Corrupted transport frame.  Ask the host to resend the
			 * currently expected command.
			 */
			size_t len = status_begin(expected_seq,
						  STATUS_COMMAND_NOT_EXECUTED, true);
			s_status[len++] = (rc == -EMSGSIZE) ?
					  CAUSE_COMMAND_TOO_LONG :
					  CAUSE_TRANSPORT_INTEGRITY;
			send_status(uart_dev, s_status, len);
			continue;
		}
		if (rc < 0) {
			goto out;
		}
		if (packet_len < 2u) {
			continue; /* not a valid packet (need seq + cmd) */
		}

		uint8_t seq_field = s_packet[0];
		uint8_t seq = seq_field & SEQ_MASK;
		bool sync = (seq_field & SEQ_SYNC_BIT) != 0u;
		uint8_t cmd = s_packet[1];
		const uint8_t *data = &s_packet[2];
		uint16_t data_len = packet_len - 2u;

		/* --- Sequence-number gate (mirrors the MDFU reference client). */
		if (!(sync || seq == expected_seq)) {
			if (seq == last_seq && last_status_len > 0u) {
				/* Duplicate of last command: resend cached reply. */
				send_status(uart_dev, s_last, last_status_len);
			} else {
				size_t len = status_begin(expected_seq,
						STATUS_COMMAND_NOT_EXECUTED, true);
				s_status[len++] = CAUSE_SEQUENCE_INVALID;
				send_status(uart_dev, s_status, len);
			}
			continue;
		}

		if (sync) {
			expected_seq = seq;
		}

		/* --- Execute the command, building a status packet in s_status. */
		size_t len = 0u;
		bool done = false;

		switch (cmd) {
		case CMD_GET_CLIENT_INFO:
			len = handle_get_client_info(seq);
			break;

		case CMD_START_TRANSFER:
			/* Restart the write context for a fresh image. */
			rc = flash_img_init(&s_flash_ctx);
			if (rc < 0) {
				len = status_begin(seq, STATUS_ABORT_FILE_TRANSFER, false);
				s_status[len++] = ABORT_GENERIC_CLIENT_ERROR;
			} else {
				transfer_active = true;
				len = status_begin(seq, STATUS_SUCCESS, false);
			}
			break;

		case CMD_WRITE_CHUNK:
			rc = flash_img_buffered_write(&s_flash_ctx, data, data_len, false);
			if (rc < 0) {
				len = status_begin(seq, STATUS_ABORT_FILE_TRANSFER, false);
				s_status[len++] = ABORT_WRITE_ERROR;
			} else {
				len = status_begin(seq, STATUS_SUCCESS, false);
			}
			break;

		case CMD_GET_IMAGE_STATE:
			/* Flush any buffered tail, then sanity-check the image
			 * header.  MCUboot performs full cryptographic verification
			 * on the next boot. */
			rc = flash_img_buffered_write(&s_flash_ctx, NULL, 0u, true);
			len = status_begin(seq, STATUS_SUCCESS, false);
			s_status[len++] = (rc == 0 && slot1_image_looks_valid()) ?
					  IMAGE_STATE_VALID : IMAGE_STATE_INVALID;
			break;

		case CMD_END_TRANSFER:
			len = status_begin(seq, STATUS_SUCCESS, false);
			done = true;
			break;

		default:
			len = status_begin(seq, STATUS_COMMAND_NOT_SUPPORTED, false);
			break;
		}

		send_status(uart_dev, s_status, len);

		/* Cache this reply for duplicate-command retransmission. */
		memcpy(s_last, s_status, len);
		last_seq = expected_seq;
		last_status_len = len;

		/* Advance the expected sequence number (wraps mod 32). */
		expected_seq = (uint8_t)((expected_seq + 1u) & SEQ_MASK);

		if (done) {
			rc = 0;
			goto out;
		}
	}

out:
	rx_transport_stop(uart_dev);
	return rc;
}
