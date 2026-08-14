# MCUboot Firmware Upgrade over MDFU (UART)

Demonstrate MCUboot secure boot and firmware upgrade where the new image is
delivered **over the serial console using the Microchip Device Firmware Update
(MDFU) protocol** — no debug probe is needed for the upgrade itself.

This is the MDFU-enabled variant of the `mcuboot_basic` demo: it adds a UART
MDFU client ([uart_dfu/](uart_dfu/)) and a `'u'` console command that streams a
signed image into MCUboot's secondary slot (slot1) from the host `pymdfu` tool.

## Overview

- **app_v1** (v1.0.0): slow LED blink (1 s period)
- **app_v2** (v2.0.0): fast LED blink (250 ms period)

app_v1 boots and confirms itself. Pressing **`u`** on the serial console drops
the device into MDFU receive mode; `pymdfu` then streams a new signed image into
slot1. On the next reboot MCUboot verifies the ECDSA-P256 signature and swaps
slot0/slot1, with automatic rollback if the image is not confirmed.

> **Warning:** MCUboot's built-in test key (`root-ec-p256.pem`) is **public and
> well-known**. Replace it with a private key for production.

## Build

Replace `<board>` with your target (see `boards.yaml`).

These are **two separate builds** — run both commands:

```console
# Build 1: MCUboot + app_v1 -> merged hex (slot1 left empty)
$ west build --pristine --sysbuild -b <board> \
    microchip-zsdk/applications/mcuboot/mcuboot_mdfu/app_v1

# Build 2: app_v2 -> the image delivered over MDFU (auto-signed)
#   -d build_app_v2 is required; without it the output lands in build/
#   and the pymdfu path below will not match.
$ west build --pristine -b <board> -d build_app_v2 \
    microchip-zsdk/applications/mcuboot/mcuboot_mdfu/app_v2
```

The merged hex produced by Build 1 is `build/zephyr/merged_<board>_<device>.hex` (e.g. `build/zephyr/merged_pic32ck_sg01_cult_pic32ck2051sg01144.hex`). The MDFU payload produced by Build 2 is `build_app_v2/zephyr/zephyr.signed.bin`.

## Flash

Flash the merged hex using the J-Link runner. The default `mplab_ipe` programmer
erases flash between each image write, which destroys previously programmed
images in multi-image builds. Use `--runner jlink` to avoid this:

```console
$ west flash --runner jlink
```

## Upgrade over MDFU

```console
$ pip install pymdfu
```

1. Confirm app_v1 is running (slow blink); the banner lists the `'u'` option.
2. Press **`u`** on the board console to enter MDFU receive mode, then **close
   the terminal** — the UART now carries raw binary MDFU frames.
   > **Note:** the device only waits **30 s** for the first MDFU frame (idle
   > timeout) before giving up and returning to normal operation. Have the
   > `pymdfu` command below ready to run immediately after pressing `'u'` and
   > closing the terminal, or the device will time out and you will need to
   > press `'u'` again.
3. From the host (replace `<port>` with your serial port):

   ```console
   $ pymdfu update --tool serial --image build_app_v2/zephyr/zephyr.signed.bin \
         --port <port> --baudrate 115200
   ```

4. On success the device reboots, MCUboot verifies and swaps, and app_v2 runs
   (fast blink).

   > **Warning:** app_v2 does not confirm the image (`boot_write_img_confirmed()`)
   > until ~10.7 s after boot (startup blink + `APP_STARTUP_DELAY_MS` slow-blink
   > wait + GPIO settle in `app_state_init()`). Resetting the board during that
   > window — even though the fast blink is already visible — leaves the swap
   > unconfirmed, so MCUboot **reverts to app_v1** on the next boot. Wait for the
   > full banner on the serial console (or ~11 s) before resetting.

### Expected console output

After pressing `'u'`, the device always prints:

```console
MDFU: waiting for host to start image transfer...
```

**Success** — `pymdfu` connects within 30 s and completes the transfer before
the device's inactivity timeout:

```console
MDFU: waiting for host to start image transfer...
Transfer complete - upgrading to v2.0.0...

Upgrade to v2.0.0 requested
Rebooting in 3...
2...
1...
```

The device then resets; MCUboot verifies the signature and swaps slot0/slot1.

**Timeout** — no MDFU frame arrives within 30 s (e.g. `pymdfu` was started too
late, wrong port/baudrate, or the terminal was not closed before the transfer
started):

```console
MDFU: waiting for host to start image transfer...

DFU transfer failed - resuming normal operation
```

The device returns to `APP_STATE_SERVICE_TASKS` (LED resumes blinking at the
normal rate) — press `'u'` again to retry.

Smoke-test the transport without transferring an image:

```console
$ pymdfu client-info --tool serial --port <port> --baudrate 115200
```

## Notes

- RX is **interrupt-driven** into a ring buffer (`CONFIG_UART_INTERRUPT_DRIVEN=y`,
  `CONFIG_RING_BUFFER=y` in `prj.conf`) — a polled read overruns the FIFO-less
  UART RX register at 115200 baud.
- `CONFIG_APP_MDFU_AUTO_ENTER` (in `app_vN/Kconfig`, default off) enters receive
  mode automatically at boot, so the host can connect without pressing `'u'`.
- The 30 s idle timeout (waiting for the first frame) and the 2 s per-byte
  timeout (once a transfer is in progress) are defined by `IDLE_TIMEOUT_MS`
  and `BYTE_TIMEOUT_MS` in `uart_dfu/uart_dfu.c`. If a transfer stalls partway
  through, only the shorter 2 s timeout applies from that point on.

See [uart_dfu/README.md](uart_dfu/README.md) for the protocol, frame format, and
porting notes.
