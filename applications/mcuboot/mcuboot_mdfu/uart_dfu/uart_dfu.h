/*
 * Copyright (c) 2026 Microchip Technology Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * MDFU (Microchip Device Firmware Update) client - UART transport, public API
 *
 * Implements the device (client) side of the MDFU protocol v1.3.0 over the
 * UART serial transport, receiving a firmware upgrade image from an MDFU host
 * (e.g. Microchip's `pymdfu` tool) and writing it to MCUboot's secondary slot
 * (slot1).
 *
 * Transport frame:
 *   [0x56 START][ escaped( packet + checksum_LE16 ) ][0x9E END]
 *   Byte-stuffing (escape 0xCC): 0x56->CC A9, 0x9E->CC 61, 0xCC->CC 33.
 *   checksum = two's-complement 16-bit sum of the packet's LE16 words,
 *              bitwise-inverted (odd length is zero-padded before summing).
 *
 * Packet: [seq_field:1][command|status:1][data:0..N]
 *   seq_field bits 0-4 = sequence number (0..31),
 *             bit 7 = sync (command packet), bit 6 = resend (status packet).
 *
 * Transfer flow (device perspective):
 *   uart_dfu_receive() called
 *     <- GET_CLIENT_INFO (sync)  -> SUCCESS + client-info TLV
 *     <- START_TRANSFER          -> SUCCESS
 *     <- WRITE_CHUNK * N         -> SUCCESS per chunk (streamed to slot1)
 *     <- GET_IMAGE_STATE         -> SUCCESS + VALID/INVALID (header sanity check;
 *                                   MCUboot performs full signature verification
 *                                   on the next boot, with automatic rollback)
 *     <- END_TRANSFER            -> SUCCESS
 *   returns 0
 *
 * Caller must NOT call printk() while uart_dfu_receive() is running;
 * all UART traffic is raw binary (MDFU frames) during the transfer.
 */

#ifndef UART_DFU_H
#define UART_DFU_H

#include <zephyr/device.h>

/*
 * Maximum MDFU packet data bytes accepted per command, advertised to the host
 * as the client buffer size (the host chunks WRITE_CHUNK payloads to match).
 */
#define UART_DFU_CHUNK_SIZE 256

/**
 * @brief Receive a firmware image via the MDFU protocol (UART transport).
 *
 * Acts as an MDFU client: blocks servicing MDFU commands until the host
 * completes the transfer (END_TRANSFER) and the image is written to MCUboot's
 * secondary flash slot, or until an error/timeout occurs.
 *
 * @param uart_dev  UART device (typically DT_CHOSEN(zephyr_console)).
 *                  printk/console output must be suspended during the call.
 * @return 0 on success, negative errno on failure.
 */
int uart_dfu_receive(const struct device *uart_dev);

#endif /* UART_DFU_H */
