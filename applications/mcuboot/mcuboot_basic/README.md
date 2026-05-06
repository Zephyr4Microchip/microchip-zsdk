# MCUboot Basic Firmware Upgrade

Demonstrate MCUboot secure boot and firmware upgrade on PIC32CK SG01 and GC01.

## Overview

This application demonstrates a complete MCUboot boot chain with secure
firmware upgrade (swap-using-move) on the Microchip PIC32CK Curiosity Ultra
boards. Supported boards:

- **PIC32CK SG01 Curiosity Ultra** (`pic32ck_sg01_cult`) — Cortex-M33 with TrustZone
- **PIC32CK GC01 Curiosity Ultra** (`pic32ck_gc01_cult`) — Cortex-M33 without TrustZone

Both boards share the same SoC (PIC32CK2051) and flash topology:

- **MCUboot bootloader** running in BFM (Boot Flash Memory, 128 KB)
- **app_v1** (v1.0.0): slow LED blink (1 s period)
- **app_v2** (v2.0.0): fast LED blink (250 ms period)

The user triggers the upgrade by pressing the SW0 button or sending `y`
over the serial console. MCUboot swaps slot0 and slot1 images on the next
reboot, with automatic rollback if the new image is not confirmed.

Images are signed with ECDSA-P256 using MCUboot's built-in test key
(`root-ec-p256.pem`). MCUboot verifies the signature on every boot.

> **Warning:** The test key shipped with MCUboot is **public and well-known**.
> It must be replaced with a private key for production deployments.

## Memory Layout (PIC32CK2051 — SG01 and GC01)

The PIC32CK2051 has two physically separate flash regions exposed as a
unified flash device (`flash0`): BFM (Boot Flash Memory) and PFM (Program
Flash Memory). MCUboot resides in BFM and manages application images in PFM.

### PIC32CK2051 (SG01 / GC01) — 2 MB PFM

| Region                 | Address      | Size              |
|------------------------|--------------|-------------------|
| BFM (MCUboot)          | 0x08000000   | 128 KB            |
| Unimplemented          | 0x08020000   | ~62 MB (gap)      |
| PFM slot0 (active app) | 0x0C000000   | 1008 KB (0xFC000) |
| PFM slot1 (upgrade)    | 0x0C0FC000   | 1008 KB (0xFC000) |
| PFM storage            | 0x0C1F8000   | 32 KB             |

> **Note:** This layout is specific to the PIC32CK2051 variant (2 MB PFM +
> 128 KB BFM). Other PIC32CK variants with smaller PFM will require
> adjusted slot sizes in the board overlay and signing commands.

## Requirements

- Microchip PIC32CK SG01 or GC01 Curiosity Ultra board
- J-Link debug probe (on-board or external)
- Serial terminal (115200 baud, 8N1)
- Zephyr SDK with `arm-none-eabi` toolchain
- Python 3.12+ with `imgtool` dependencies (installed via MCUboot)

## Project Structure

```
mcuboot_basic/
├── app_common.h                  Shared header (state machine, update trigger)
├── app_v1/                       Application v1.0.0 (slow blink)
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── boards/<board>.conf       Board-specific app configuration
│   └── src/main.c
├── app_v2/                       Application v2.0.0 (fast blink)
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── boards/<board>.conf       Board-specific app configuration
│   └── src/main.c
├── mcuboot/                      Board-specific MCUboot configuration
│   └── boards/
│       ├── <board>.conf
│       └── <board>.overlay
├── boards.yaml                   Board parameters (slot sizes, addresses, etc.)
└── README.md                     This file
```

## Building and Running

### Using West Command

The `west mcuboot-build` command automates the full workflow: build MCUboot,
build both apps, sign images, and optionally flash to the target. This command
can be run from anywhere within the west workspace.

```console
# Build for a specific board
$ west mcuboot-build -b pic32ck_sg01_cult

# Build and flash to target
$ west mcuboot-build -b pic32ck_sg01_cult --flash

# Clean build (pristine)
$ west mcuboot-build -b pic32ck_sg01_cult --pristine

# List available boards
$ west mcuboot-build --list-boards

# Use a custom application directory
$ west mcuboot-build -b pic32ck_sg01_cult -a path/to/my_mcuboot_app

# Full help
$ west mcuboot-build --help
```

Build outputs are placed in the `<zephyr>/` directory:
- `build_mcuboot/` — MCUboot bootloader
- `build_app_v1/` — Application v1 with signed images
- `build_app_v2/` — Application v2 with signed images

### Manual Build Steps

If you prefer to build manually, follow these steps from the Zephyr
directory (`<workspace>/zephyr`). Replace `<BOARD>` with
`pic32ck_sg01_cult` or `pic32ck_gc01_cult`. Board parameters are defined
in `boards.yaml`.

**Step 1: Build MCUboot**

```console
$ west build -b <BOARD> -d build_mcuboot \
    ../bootloader/mcuboot/boot/zephyr --pristine -- \
    -DDTC_OVERLAY_FILE="../microchip-zsdk/applications/mcuboot/mcuboot_basic/mcuboot/boards/<BOARD>.overlay" \
    -DOVERLAY_CONFIG="../microchip-zsdk/applications/mcuboot/mcuboot_basic/mcuboot/boards/<BOARD>.conf"
```

**Step 2: Build and sign app_v1**

```console
$ west build -b <BOARD> -d build_app_v1 \
    ../microchip-zsdk/applications/mcuboot/mcuboot_basic/app_v1 --pristine

$ python ../bootloader/mcuboot/scripts/imgtool.py sign \
    --key ../bootloader/mcuboot/root-ec-p256.pem \
    --header-size <HEADER_SIZE> --align <ALIGN> --version 1.0.0 --slot-size <SLOT_SIZE> \
    build_app_v1/zephyr/zephyr.bin build_app_v1/zephyr/zephyr.signed.bin

$ arm-zephyr-eabi-objcopy -I binary -O ihex --change-addresses=<SLOT0_ADDR> \
    build_app_v1/zephyr/zephyr.signed.bin build_app_v1/zephyr/zephyr.signed.slot0.hex
```

**Step 3: Build and sign app_v2**

```console
$ west build -b <BOARD> -d build_app_v2 \
    ../microchip-zsdk/applications/mcuboot/mcuboot_basic/app_v2 --pristine

$ python ../bootloader/mcuboot/scripts/imgtool.py sign \
    --key ../bootloader/mcuboot/root-ec-p256.pem \
    --header-size <HEADER_SIZE> --align <ALIGN> --version 2.0.0 --slot-size <SLOT_SIZE> \
    build_app_v2/zephyr/zephyr.bin build_app_v2/zephyr/zephyr.signed.bin

$ arm-zephyr-eabi-objcopy -I binary -O ihex --change-addresses=<SLOT1_ADDR> \
    build_app_v2/zephyr/zephyr.signed.bin build_app_v2/zephyr/zephyr.signed.slot1.hex
```

**Board parameters** (from `boards.yaml`):

| Board | SLOT_SIZE | SLOT0_ADDR | SLOT1_ADDR | HEADER_SIZE | ALIGN |
|-------|-----------|------------|------------|-------------|-------|
| `pic32ck_sg01_cult` | `0xFC000` | `0x0C000000` | `0x0C0FC000` | `0x400` | `8` |
| `pic32ck_gc01_cult` | `0xFC000` | `0x0C000000` | `0x0C0FC000` | `0x400` | `8` |

**Step 4: Flash**

Flash images via J-Link:

| Board | JLink Device |
|-------|--------------|
| `pic32ck_sg01_cult` | `PIC32CK2051SG01144` |
| `pic32ck_gc01_cult` | `PIC32CK2051GC01144` |

```console
$ JLinkExe -device <JLINK_DEVICE> -if SWD -speed 4000 -autoconnect 1 \
    -CommanderScript <(printf "r\nh\nerase\nloadfile build_mcuboot/zephyr/zephyr.hex\nloadfile build_app_v1/zephyr/zephyr.signed.slot0.hex\nloadfile build_app_v2/zephyr/zephyr.signed.slot1.hex\nr\ng\nq\n")
```

## Expected Behavior

After flashing the combined image, the following sequence is expected:

1. **MCUboot boots** and validates the signature of app_v1 in slot0.

2. **app_v1 starts**: LED 0 blinks slowly (1 s period).

   Serial output:
   ```
   MCUboot Demo v1.0.0 - Slow Blink (1s)
   Press SW0 or 'y' to upgrade to v2.0.0

   Image v1.0.0 confirmed
   ```

3. **User triggers upgrade**: Press **SW0** button or send `y` via serial.

   Serial output:
   ```
   Upgrade to v2.0.0 requested
   Rebooting in 3...
   2...
   1...
   ```

   The LED blinks rapidly 6 times as visual confirmation.

4. **MCUboot performs swap**: On reboot, MCUboot detects the upgrade
   request, verifies app_v2's signature, and swaps slot0/slot1.

5. **app_v2 starts**: LED 0 blinks fast (250 ms period).

   Serial output:
   ```
   MCUboot Demo v2.0.0 - Fast Blink (250ms)
   Upgrade successful!
   Press SW0 or 'y' to upgrade to v3.0.0

   Image v2.0.0 confirmed
   ```

6. **Image confirmed**: app_v2 calls `boot_write_img_confirmed()` to
   make the upgrade permanent. Without confirmation, MCUboot would
   revert to app_v1 on the next reboot.

## Board Differences

| Feature | SG01 | GC01 |
|---------|------|------|
| CPU | Cortex-M33 | Cortex-M33 |
| TrustZone | Yes (auto-enabled) | No |
| PFM size | 2 MB | 2 MB |
| Slot size | 1008 KB | 1008 KB |
| Clock | 120 MHz | 120 MHz |
| LED0 | PD20 (secure) | PD20 |
| Console | SERCOM5 | SERCOM5 |
| JLink device | PIC32CK2051SG01144 | PIC32CK2051GC01144 |

TrustZone is handled automatically by the SoC `Kconfig.defconfig` — it is
enabled only for SG series boards. No application-level configuration is
needed.

## MCUboot Configuration

Key MCUboot settings (`mcuboot/boards/<BOARD>.conf`):

| Option | Value | Description |
|--------|-------|-------------|
| `CONFIG_BOOT_SWAP_USING_MOVE` | `y` | Swap algorithm with rollback capability |
| `CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256` | `y` | ECDSA-P256 signature verification |
| `CONFIG_BOOT_VALIDATE_SLOT0` | `y` | Verify primary slot on every boot |
| `CONFIG_MCUBOOT_DOWNGRADE_PREVENTION` | `y` | Reject images with lower version numbers |
| `CONFIG_BOOT_BOOTSTRAP` | `y` | Allow initial image installation from slot1 |
| `CONFIG_BOOT_MAX_IMG_SECTORS` | 256 | Per-board sector count |

## Customization

### Changing the Signing Key

To use a custom key pair:

1. Generate a new ECDSA-P256 key:

   ```console
   $ python imgtool.py keygen -k my-signing-key.pem -t ecdsa-p256
   ```

2. Update `boards/mcuboot_<BOARD>.conf`:

   ```
   CONFIG_BOOT_SIGNATURE_KEY_FILE="path/to/my-signing-key.pem"
   ```

3. Update `app_v1/prj.conf` and `app_v2/prj.conf`:

   ```
   CONFIG_MCUBOOT_SIGNATURE_KEY_FILE="path/to/my-signing-key.pem"
   ```

4. Update the `--key` argument in the build scripts.

### Adding More Application Versions

Copy `app_v2/` to `app_v3/` and modify:

- `APP_VERSION` and `TARGET_VERSION` in `main.c`
- `BLINK_INTERVAL_MS` for visual differentiation
- Increment `--version` in the signing command
- Set `--change-addresses` to `<SLOT1_ADDR>` for the target board

### Adding a New Board

To add support for another Microchip variant:

1. Add board entry to `boards.yaml` with required parameters:
   - `jlink_device`: JLink device identifier
   - `slot_size`: Slot size in hex (e.g., `0xFC000`)
   - `slot0_addr`: Slot0 address in hex
   - `slot1_addr`: Slot1 address in hex
   - `header_size`: Image header size in hex
   - `align`: Image alignment

2. Create board-specific configuration files:
   - `mcuboot/boards/<board>.conf` — MCUboot configuration
   - `mcuboot/boards/<board>.overlay` — MCUboot device tree overlay
   - `app_v1/boards/<board>.conf` — App v1 configuration
   - `app_v2/boards/<board>.conf` — App v2 configuration

3. Ensure the board DTS has the correct `flash0` partitions

Run `west mcuboot-build --list-boards` to verify the new board is detected.

## Troubleshooting

**MCUboot does not jump to application**

Check the serial console for MCUboot log output. Common causes:

- Image signature verification failure (wrong key)
- Image header size mismatch (must be `0x400`)
- Slot size mismatch between overlay and `imgtool`

**Application boots but upgrade does not work**

Ensure app_v2 is flashed to slot1 (see per-board addresses above). Verify
the serial output shows "Upgrade to v2.0.0 requested" before reboot.

**Device resets immediately after pressing SW0**

This was a known bug in the flash driver (buffer indexing).
Ensure you are using the latest `flash_mchp_nvmctrl_g3.c` with
the separated `src` pointer fix.
