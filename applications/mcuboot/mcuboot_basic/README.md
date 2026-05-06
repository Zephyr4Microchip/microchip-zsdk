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
> adjusted slot sizes in the board DTS and signing commands.

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
├── boards.yaml                   Board parameters (slot sizes, addresses, etc.)
├── README.md                     This file
├── app_v1/                       Application v1.0.0 (slow blink)
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── sysbuild.conf             Sysbuild settings (boot mode, signature type)
│   ├── sysbuild/
│   │   ├── mcuboot.conf          MCUboot config fragment (merged with upstream)
│   │   └── mcuboot.overlay       DTS overlay (chosen nodes for BFM linking)
│   ├── boards/<board>.conf       Board-specific app configuration
│   └── src/main.c
└── app_v2/                       Application v2.0.0 (fast blink)
    ├── CMakeLists.txt
    ├── prj.conf
    ├── sysbuild.conf             Sysbuild settings (boot mode, signature type)
    ├── sysbuild/
    │   ├── mcuboot.conf          MCUboot config fragment (merged with upstream)
    │   └── mcuboot.overlay       DTS overlay (chosen nodes for BFM linking)
    ├── boards/<board>.conf       Board-specific app configuration
    └── src/main.c
```

### MCUboot Configuration Architecture

MCUboot configuration uses the **standard Zephyr sysbuild approach**: a
`sysbuild/mcuboot.conf` fragment that is MERGED with MCUboot's own upstream
`prj.conf` (not replacing it). This ensures upstream bug fixes and new
settings are automatically inherited.

| File | Purpose |
|------|---------|
| `app_vN/sysbuild/mcuboot.conf` | Config fragment merged with MCUboot's prj.conf |
| `app_vN/sysbuild/mcuboot.overlay` | DTS overlay: sets `zephyr,flash` and `zephyr,code-partition` for BFM |

Both build methods use identical configuration:

- **Sysbuild**: auto-discovers `sysbuild/mcuboot.conf` as `EXTRA_CONF_FILE`
  and `sysbuild/mcuboot.overlay` as `DTC_OVERLAY_FILE` (standard Zephyr
  mechanism in `sysbuild_extensions.cmake`).
- **west mcuboot-build**: passes `-DOVERLAY_CONFIG` and `-DDTC_OVERLAY_FILE`
  pointing to `app_v1/sysbuild/mcuboot.conf` and `mcuboot.overlay`.

The fragment only contains settings that MCUboot upstream does NOT provide.
Settings already in MCUboot's `prj.conf` (e.g., `CONFIG_PM=n`,
`CONFIG_MAIN_STACK_SIZE=10240`, `CONFIG_FLASH=y`) are not duplicated.

## Building and Running

Two build methods are available, each targeting a different use case:

> **Important — scope difference between the two methods:**
>
> - **`west mcuboot-build`** is a **demo-only** command. It builds and programs
>   the complete system (MCUboot + app_v1 + app_v2) in a single invocation,
>   providing a ready-to-run firmware upgrade demonstration out of the box.
>
> - **Sysbuild** (`west build --sysbuild`) builds only MCUboot + one
>   application (e.g., app_v1) and programs them together. The upgrade image
>   (app_v2) is **not** built or programmed — it is the user's responsibility
>   to build, sign, and deliver app_v2 through their own update mechanism
>   (e.g., OTA, UART loader, external programmer). This reflects the real-world
>   workflow where the initial firmware is factory-programmed and subsequent
>   upgrades are delivered separately.

### Method 1: Sysbuild (recommended for development)

Sysbuild builds MCUboot and the application as a multi-image project in a
single `west build` invocation. It produces a merged hex file for easy flashing.
Only the bootloader and the primary application (slot0) are built and programmed.

```console
# Build MCUboot + app_v1 (from any directory in the workspace)
$ west build --sysbuild -b pic32ck_sg01_cult \
    ../microchip-zsdk/applications/mcuboot/mcuboot_basic/app_v1

# Pristine build
$ west build --pristine --sysbuild -b pic32ck_sg01_cult \
    ../microchip-zsdk/applications/mcuboot/mcuboot_basic/app_v1

# Flash the merged hex (MCUboot + signed app in one shot)
$ west flash
```

Build outputs:
- `build/mcuboot/zephyr/zephyr.hex` — MCUboot bootloader (BFM)
- `build/app_v1/zephyr/zephyr.signed.hex` — Signed application (PFM)
- `build/merged_*.hex` — Combined hex for single-operation flash

> **Note on flashing**: Use the merged hex file or `west flash` with a
> single-operation programmer. Multi-domain flashing with `west flash`
> may cause chip erase between domains, destroying MCUboot before the
> app is programmed.

### Method 2: West Extension Command (full demo workflow)

The `west mcuboot-build` command is designed specifically for this demo. It
automates the complete workflow: build MCUboot, build both apps (v1 + v2),
sign all images, and optionally flash the entire system via J-Link. After
flashing, the board is immediately ready to demonstrate the firmware upgrade
(press SW0 to trigger the swap from v1 to v2).

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
    -DOVERLAY_CONFIG="../microchip-zsdk/applications/mcuboot/mcuboot_basic/app_v1/sysbuild/mcuboot.conf" \
    -DDTC_OVERLAY_FILE="../microchip-zsdk/applications/mcuboot/mcuboot_basic/app_v1/sysbuild/mcuboot.overlay"
```

> **Note:** `-DOVERLAY_CONFIG` merges with MCUboot's own `prj.conf` (unlike
> `-DCONF_FILE` which replaces it). This ensures upstream fixes are preserved.

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

The effective MCUboot configuration is the combination of:

1. **MCUboot upstream `prj.conf`** (base — provides standard defaults)
2. **`BOOTLOADER_image_default.cmake`** (sysbuild forces boot mode + signature)
3. **`sysbuild/mcuboot.conf`** (our fragment — only additions/overrides)

### Provided by MCUboot upstream (DO NOT duplicate)

| Option | Value | Source |
|--------|-------|--------|
| `CONFIG_PM` | `n` | MCUboot prj.conf |
| `CONFIG_MAIN_STACK_SIZE` | `10240` | MCUboot prj.conf |
| `CONFIG_FLASH` | `y` | MCUboot prj.conf |
| `CONFIG_LOG` / `CONFIG_LOG_MODE_MINIMAL` | `y` | MCUboot prj.conf |
| `CONFIG_PICOLIBC` / `CONFIG_CBPRINTF_NANO` | `y` | MCUboot prj.conf |

### Forced by sysbuild infrastructure (DO NOT duplicate)

| Option | Value | Source |
|--------|-------|--------|
| `CONFIG_BOOT_SWAP_USING_MOVE` | `y` | BOOTLOADER_image_default.cmake |
| `CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256` | `y` | BOOTLOADER_image_default.cmake |
| `CONFIG_BOOT_SIGNATURE_KEY_FILE` | `"root-ec-p256.pem"` | MCUboot Kconfig default |

### Our fragment (`sysbuild/mcuboot.conf`)

| Option | Value | Description |
|--------|-------|-------------|
| `CONFIG_FLASH_MAP` | `y` | Flash partition map for slot access |
| `CONFIG_BOOT_VALIDATE_SLOT0` | `y` | Verify primary slot on every boot |
| `CONFIG_MCUBOOT_DOWNGRADE_PREVENTION` | `y` | Reject images with lower version numbers |
| `CONFIG_BOOT_BOOTSTRAP` | `y` | Allow initial image installation from slot1 |
| `CONFIG_SERIAL` / `CONFIG_CONSOLE` | `y` | Serial console for boot logs |
| `CONFIG_GPIO` | `y` | LED visual feedback |
| `CONFIG_MULTITHREADING` | `y` | Required by flash driver (ISR synchronization) |
| `CONFIG_ROMSTART_RELOCATION_ROM` | `n` | MCUboot lives entirely in BFM, no relocation |
| `CONFIG_BOOT_INTR_VEC_RELOC` | `y` | Update VTOR when jumping from BFM to PFM |
| `CONFIG_BOOT_MAX_IMG_SECTORS` | `256` | Sector count (slot_size / erase_block_size) |

## Customization

### Changing the Signing Key

To use a custom key pair:

1. Generate a new ECDSA-P256 key:

   ```console
   $ python imgtool.py keygen -k my-signing-key.pem -t ecdsa-p256
   ```

2. Add to `app_vN/sysbuild/mcuboot.conf`:

   ```
   CONFIG_BOOT_SIGNATURE_KEY_FILE="path/to/my-signing-key.pem"
   ```

3. Update the `--key` argument in signing commands (or in `boards.yaml`
   if using `west mcuboot-build`).

### Adding More Application Versions

Copy `app_v2/` to `app_v3/` and modify:

- `APP_VERSION` and `TARGET_VERSION` in `main.c`
- `BLINK_INTERVAL_MS` for visual differentiation
- Increment `--version` in the signing command
- Set `--change-addresses` to `<SLOT1_ADDR>` for the target board
- Copy `app_v2/sysbuild.conf` and `app_v2/sysbuild/` directory as-is

### Adding a New Board

To add support for another Microchip variant:

1. Add board entry to `boards.yaml` with required parameters:
   - `jlink_device`: JLink device identifier
   - `slot_size`: Slot size in hex (e.g., `0xFC000`)
   - `slot0_addr`: Slot0 address in hex
   - `slot1_addr`: Slot1 address in hex
   - `header_size`: Image header size in hex
   - `align`: Image alignment

2. If the new board needs different MCUboot settings (different sector count,
   relocation behavior, etc.), add them to `app_vN/sysbuild/mcuboot.conf`.
   For boards sharing the same SoC family, the existing settings likely work.

3. Optionally create board-specific app configuration:
   - `app_v1/boards/<board>.conf` — App v1 overrides (optional)
   - `app_v2/boards/<board>.conf` — App v2 overrides (optional)

4. Ensure the board DTS has the correct `flash0` partitions with:
   - `boot_partition` in BFM
   - `slot0_partition` and `slot1_partition` in PFM
   - `ranges;` property on the partitions node

5. The `sysbuild/mcuboot.overlay` works for any board that uses
   `flash0` with `boot_partition` — no per-board overlay needed.

Run `west mcuboot-build --list-boards` to verify the new board is detected.

## Troubleshooting

**MCUboot does not jump to application**

Check the serial console for MCUboot log output. Common causes:

- Image signature verification failure (wrong key)
- Image header size mismatch (must be `0x400` for PIC32CK)
- Slot size mismatch between DTS partition and `imgtool --slot-size`
- Missing `CONFIG_MAIN_STACK_SIZE=10240` causing stack overflow during
  ECDSA verification (symptom: Usage Fault immediately after validation)

**Application boots but upgrade does not work**

Ensure app_v2 is flashed to slot1 (see per-board addresses above). Verify
the serial output shows "Upgrade to v2.0.0 requested" before reboot.

**Sysbuild flash erases MCUboot**

When using `west flash` with sysbuild multi-domain builds, each domain may
trigger a chip erase. The second erase destroys MCUboot (BFM) before the app
(PFM) is programmed. Solution: use the merged hex file (`merged_*.hex`) for
single-operation flashing, or use `west mcuboot-build --flash`.

**Device resets immediately after pressing SW0**

This was a known bug in the flash driver (buffer indexing).
Ensure you are using the latest `flash_mchp_nvmctrl_g3.c` with
the separated `src` pointer fix.

**Build fails with "Unknown CMake command build_info"**

Ensure you are building with `--sysbuild` flag. The `sysbuild/mcuboot.conf`
fragment is auto-discovered by the sysbuild infrastructure — no custom
`CMakeLists.txt` is needed in the `sysbuild/` directory.
