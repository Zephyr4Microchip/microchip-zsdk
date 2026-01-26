# Microchip zSDK

**Microchip zSDK** is a unified framework offering a minimally customized Zephyr distribution, downstream modules, and Microchip-owned libraries to develop applications for Microchip devices.

---

## Features

- Distribution of non-upstreamable content
- Early access to new device support and features before upstream availability
- Driver customizations and enhancements for Microchip platforms
- Out-of-tree board support and drivers
- Documentation and sample applications
- Extended validation and test coverage for Microchip devices

---

## SDK Repositories

The Microchip zSDK uses [West](https://docs.zephyrproject.org/latest/develop/west/index.html) to manage multiple repositories. The [west.yml](../west.yml) manifest file defines the repositories and their revisions.

| Repository | Description |
|------------|-------------|
| **microchip-zsdk** | Top-level manifest repository that pulls Microchip's downstream Zephyr fork and other Microchip repositories, excluding unrelated HAL modules to reduce workspace size. Includes reference designs, sample applications, and out-of-tree drivers and board support. |
| **microchip-zephyr** | Microchip's downstream fork of Zephyr, used for early device enablement, patches pending upstream review, and non-upstreamable content. |
| **microchip-hal** | Downstream fork of hal_microchip containing ATDF device packs, pinctrl definitions for SoCs, and patches pending upstream review. |

---

## Directory Structure

This diagram shows the workspace structure after Microchip zSDK setup:

```
mchp_zephyrproject (zSDK workspace)
├── microchip-zsdk              # Main SDK repository (manifest)
│   ├── applications            # Microchip specific apps
│   ├── doc
│   ├── scripts
│   └── west.yml                # Manifest file
├── zephyr                      # Microchip Zephyr downstream
└── modules/hal/microchip       # Microchip HAL downstream
```

---

## Getting Started

For detailed installation instructions, refer to the [Getting Started with Microchip zSDK](./Getting_Started_with_Microchip_zSDK.md) guide.

