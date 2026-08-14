# MDFU Client — UART Transport

A device-side (client) implementation of the **Microchip Device Firmware Update
(MDFU) protocol**, protocol version **1.3.0**, over the UART serial transport.
It receives a signed firmware image from an MDFU host (Microchip's `pymdfu`
tool) and streams it into MCUboot's secondary slot (slot1) using Zephyr's
`flash_img` API.

This module is used by the `mcuboot_basic` demo: press `u` on the serial console
to drop the running application into MDFU receive mode. See the top-level
[README](../README.md) for the full boot/upgrade demo.

## Files

| File | Purpose |
|------|---------|
| [uart_dfu.c](uart_dfu.c) | MDFU serial-transport framing + protocol state machine |
| [uart_dfu.h](uart_dfu.h) | Public API: `uart_dfu_receive()` and `UART_DFU_CHUNK_SIZE` |

## Public API

```c
#include "uart_dfu/uart_dfu.h"

/* Blocks running an MDFU session on uart_dev until the host completes the
 * transfer (END_TRANSFER) or an error/timeout occurs.
 * Returns 0 on success (caller should reboot to let MCUboot swap), or a
 * negative errno on failure.
 */
int uart_dfu_receive(const struct device *uart_dev);
```

`uart_dev` is normally `DEVICE_DT_GET(DT_CHOSEN(zephyr_console))`. All console
output (printk) must be suspended while this runs — the UART carries raw binary
MDFU frames during the transfer.

## RX transport (interrupt-driven)

Received bytes are captured by an **interrupt-driven UART callback** into a
software ring buffer (`RX_RING_SIZE` = 512 B) and drained by the protocol loop.
This is deliberate: the console UART has **no RX FIFO**, so at 115200 baud a
byte arrives every ~87 µs and a polled read (`uart_poll_in()`) drops bytes
mid-transfer. The ISR services each byte well within that window, and because
the ring buffer keeps filling while the protocol layer blocks, `recv_byte()` can
wait on a semaphore instead of busy-polling.

Requires `CONFIG_UART_INTERRUPT_DRIVEN=y` and `CONFIG_RING_BUFFER=y` (both set in
the demo's `prj.conf`). TX uses polled `uart_poll_out()`, which cannot overrun.

## Protocol

### Transport frame

```
[0x56 START] [ escaped( packet + checksum_LE16 ) ] [0x9E END]
```

- **Byte-stuffing** (escape code `0xCC`): reserved bytes are replaced with
  `0xCC` followed by the byte's bitwise-NOT — `0x56`→`CC A9`, `0x9E`→`CC 61`,
  `0xCC`→`CC 33`.
- **Frame check sequence**: 16-bit **two's-complement sum** of the packet's
  little-endian 16-bit words, then bitwise-inverted (an odd length is padded with
  a trailing zero byte before summing). Covers the packet bytes only, appended
  little-endian, and is itself byte-stuffed.

### Packet

```
Command (host -> device): [seq_field:1][command:1][data:0..N]
Status  (device -> host): [seq_field:1][status :1][data:0..N]
```

- `seq_field` bits 0–4 = sequence number (0–31, wraps mod 32).
- bit 7 = **sync** (command packets), bit 6 = **resend** (status packets).

### Commands / statuses

| Command | Code | | Status | Code |
|---------|------|---|--------|------|
| GET_CLIENT_INFO | 0x01 | | SUCCESS | 0x01 |
| START_TRANSFER  | 0x02 | | COMMAND_NOT_SUPPORTED | 0x02 |
| WRITE_CHUNK     | 0x03 | | NOT_AUTHORIZED | 0x03 |
| GET_IMAGE_STATE | 0x04 | | COMMAND_NOT_EXECUTED | 0x04 |
| END_TRANSFER    | 0x05 | | ABORT_FILE_TRANSFER | 0x05 |

### Session flow

```
host                                   device (uart_dfu_receive)
  ── GET_CLIENT_INFO (sync) ─────────▶  flash_img_init()
  ◀───────── SUCCESS + client-info TLV  (buffer size, proto version, timeouts)
  ── START_TRANSFER ─────────────────▶  reset write context
  ◀───────── SUCCESS
  ── WRITE_CHUNK (raw image bytes) ──▶  flash_img_buffered_write() (append)
  ◀───────── SUCCESS                    ... repeated for each chunk
  ── GET_IMAGE_STATE ────────────────▶  flush + header sanity check
  ◀───────── SUCCESS + VALID/INVALID
  ── END_TRANSFER ───────────────────▶  return 0  → caller reboots
  ◀───────── SUCCESS
```

The **sequence-number handling** mirrors the MDFU reference client: a command
with the expected sequence number (or the `sync` bit set) is executed and the
number advanced; a duplicate of the last command re-sends the cached reply; any
other number is rejected with `COMMAND_NOT_EXECUTED` (`resend`) so the host
retries. Corrupted transport frames (bad checksum / escape) are likewise
answered with a `resend` request.

### GET_CLIENT_INFO response (TLV)

| Type | Bytes | Value |
|------|-------|-------|
| `BUFFER_INFO` (0x02) | 3 | buffer_size (LE16) + buffer_count (1) |
| `PROTOCOL_VERSION` (0x01) | 3 | major, minor, patch → `1.3.0` |
| `COMMAND_TIMEOUTS` (0x03) | 3 | default timeout (cmd 0 + LE16, units of 0.1 s) |

`buffer_size` is `UART_DFU_CHUNK_SIZE` (256); the host chunks WRITE_CHUNK
payloads to match.

## Image verification

`GET_IMAGE_STATE` performs a **lightweight header check** on slot1 via
`boot_read_bank_header()` — it confirms an MCUboot image header is present and
that the image size is non-zero and fits within the slot, reporting
`VALID`/`INVALID`.

**Full cryptographic signature verification is performed by MCUboot on the next
boot** (test-boot with automatic rollback on failure). The application image
deliberately does not link the bootloader's crypto validator
(`bootutil_img_validate`), which lives only in the MCUboot image — so runtime
signature checking is not available to the app in this configuration.

## Host usage (`pymdfu`)

```console
$ pip install pymdfu

# 1) press 'u' on the board console to enter MDFU receive mode, then:
$ pymdfu update --tool serial --image zephyr.signed.bin --port <PORT> --baudrate 115200

# inspect the client without transferring:
$ pymdfu client-info --tool serial --port <PORT> --baudrate 115200
```

## Configuration

Requires the following Kconfig (already set in the demo's `prj.conf`):

```
CONFIG_UART_INTERRUPT_DRIVEN=y  # interrupt-driven RX (avoids UART RX overrun)
CONFIG_RING_BUFFER=y            # RX ring buffer
CONFIG_IMG_MANAGER=y            # boot_read_bank_header(), image manager
CONFIG_MCUBOOT_BOOTUTIL_LIB=y   # bootutil_public (swap/confirm state)
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_STREAM_FLASH=y           # backs flash_img_buffered_write()
```

No CRC library is needed — the MDFU frame check sequence is a plain
two's-complement sum computed in `uart_dfu.c`.

## Porting to another application

1. Add the sources to your build (see the demo `CMakeLists.txt`):
   ```cmake
   target_sources(app PRIVATE ../uart_dfu/uart_dfu.c)
   target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
   ```
2. Ensure the Kconfig above is enabled.
3. Call `uart_dfu_receive()` on a quiescent console UART, then reboot on a `0`
   return to let MCUboot swap the image.
