# MCUboot Demo - PIC32CK-SG01

## What This Demo Does

This demo shows MCUboot firmware update functionality on the PIC32CK-SG01 board:

1. Device starts with **app_v1** (slow LED blink)
2. Press SW0 button to request firmware update
3. Device reboots and MCUboot swaps to **app_v2** (fast LED blink)
4. If app_v2 fails, MCUboot automatically rolls back to app_v1

Both applications use a common state machine architecture that makes it easy to create additional versions (app_v3, app_v4, etc.) and chain updates together.

## Build and Flash

```cmd
cd <workspace>\microchip-zsdk\applications\mcuboot\pic32ck_sg01_curiosity_ultra
build.bat
```

Replace `<workspace>` with your installation path. The script auto-detects paths from its location, so you can run it from anywhere.

This builds MCUboot, app_v1, and app_v2, then flashes everything to the device.

## Demo Flow

**Step 1: Initial boot**
- 3 quick blinks (startup indicator)
- LED blinks slowly (1 second intervals)
- Serial output: `MCUboot Demo v1.0.0 - Slow Blink (1s)`

**Step 2: Trigger update**
- Press SW0 button or enter 'y' via serial console
- 6 rapid blinks (confirmation)
- Device reboots after 3 second countdown

**Step 3: After reboot**
- MCUboot swaps slot1 → slot0
- 3 quick blinks (startup indicator)
- LED blinks fast (250ms intervals)
- Serial output: `MCUboot Demo v2.0.0 - Fast Blink (250ms)`

**Step 4: Image confirmation**
- app_v2 calls `boot_write_img_confirmed()` to prevent rollback
- Update is now permanent
- Device will continue running app_v2 on future reboots

## Memory Layout

```
BFM (flash0) @ 0x08000000:  MCUboot bootloader
PFM (flash1) @ 0x0C000000:  Application slots
  ├─ Slot 0 (Primary)   - 1008 KB @ 0x0C000000
  ├─ Slot 1 (Secondary) - 1008 KB @ 0x0C0FC000
  └─ Storage            - 32 KB   @ 0x0C1F8000
```

MCUboot lives in BFM (Boot Flash Memory), applications run from PFM (Program Flash Memory). MCUboot relocates the vector table (VTOR) to 0x0C000200 before jumping to the application.

## Application Architecture

### State Machine

All applications use a 3-state machine defined in `app_common.h`:

```
     ┌─────────────────┐
     │  APP_STATE_INIT │  • Configure peripherals
     │                 │  • Display version info
     └────────┬────────┘  • Confirm image (prevent rollback)
              │
              v
┌────────────────────────┐
│ APP_STATE_SERVICE_TASKS│  • Toggle LED (blink)
│                        │  • Check SW0 button
│    ◄───────────┐       │  • Check serial input ('y')
└────────┬───────┘       │
         │               │
         │ (SW0 or 'y')  │
         v               │
┌──────────────────────┐ │
│APP_STATE_TRIGGER_     │ │
│      UPDATE          │ │
│                      │ │
│ • Request MCUboot    │ │
│   to swap images     │ │
│ • Flash LED 6x       │ │
│ • Countdown 3s       │─┘ (never returns,
│ • Reboot device      │    system resets)
└──────────────────────┘
```

**How it works:**

1. **INIT state:** On boot, app displays version, confirms the image to MCUboot (prevents rollback), then moves to SERVICE_TASKS
2. **SERVICE_TASKS state:** Main loop - blinks LED at defined interval, monitors SW0 button and serial input. Stays here until user requests update
3. **TRIGGER_UPDATE state:** Calls `boot_request_upgrade()` to tell MCUboot to swap images on next reboot, shows visual feedback, then reboots device

**Key benefit:** Same state machine for all versions. Creating app_v3 is just changing version numbers and blink rate.

### Shared Functions

`app_common.h` provides reusable functions:

- `app_trigger_update()` - Handles update request, visual feedback, reboot
- `app_startup_blink()` - 3 quick blinks on startup
- `app_confirm_image()` - Calls `boot_write_img_confirmed()` to mark image as good

### Versions

- **app_v1** - Slow blink (1000ms), upgrades to v2
- **app_v2** - Fast blink (250ms), upgrades to v3 (if available)

## Safety Features

**Automatic rollback:** If the new firmware doesn't call `boot_write_img_confirmed()` within one boot cycle, MCUboot reverts to the previous version on next reboot.

**Downgrade prevention:** Enabled in MCUboot config - prevents rolling back to older versions.

**Empty slot handling:** If you trigger an update but slot1 is empty or invalid, MCUboot skips the swap and boots slot0 again. System always reaches a known-good state.

## Creating New Versions

To create app_v3 or higher:

1. Copy app_v2 to app_vX
2. Edit `src/main.c`:
   ```c
   #define APP_VERSION        3  // change version
   #define TARGET_VERSION     4  // next version
   #define BLINK_INTERVAL_MS  500 // unique blink rate
   ```
3. Update CMakeLists.txt to add parent include path
4. Build and sign with version X.0.0

The `app_common.h` provides all update functionality automatically.

## Testing

Unit tests are available in the `tests/` directory to verify state machine logic. Tests use **Ztest**, Zephyr's built-in testing framework.

**Ztest** provides:
- Simple test syntax with `ZTEST()` macros
- Assertions like `zassert_equal()`, `zassert_true()`
- Runs on actual hardware or simulators
- Clear PASS/FAIL reporting

**Run tests:**

```bash
cd <workspace>\zephyr
west build -p -b pic32ck_sg01_cult <workspace>\microchip-zsdk\applications\mcuboot\pic32ck_sg01_curiosity_ultra\tests
west flash
```

Open serial monitor (115200 baud) to see results.

**Tests cover:**
- State transitions (INIT → SERVICE_TASKS → TRIGGER_UPDATE)
- Version handling
- Update request logic
- Blink interval configuration

9 tests validate the state machine without requiring MCUboot or GPIO hardware. See `tests/README.md` for details.
