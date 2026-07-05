# Microchip zSDK v4.4.0.0 Release Notes

Microchip zSDK v4.4.0.0 is based on the Zephyr v4.4.0 release, integrated with Microchip-specific enhancements, board support, and driver improvements.

## Release Highlights

- **5 new board supports** added
- Added **UDC** driver support for the SAMA7G5 UDPHS peripheral
- Added **UDC** driver for High-Speed USB peripherals on the PIC32CK SG/GC family
- Added **UDC** driver for Full-Speed USB peripherals on the SAM D5x/E5x, PIC32CX SG, and PIC32CK SG/GC families
- Added **SDHC** (SD Host Controller) driver for Microchip devices
- Added **QSPI Flash** driver for SAMA7G5 MPU devices
- Added **Flash NVMCTRL** driver for PIC32CM JH and PIC32CZ CA80/CA90 families
- Added **DMA** driver for PIC32CZ CA80/CA90 families
- Added **ADC** driver for PIC32CK SG/GC families
- Added **Ethernet GMAC** driver support for the SAM D5x/E5x family
- Enhanced the **OA TC6 Ethernet** driver with full-duplex operation
- Added **MPLAB IPE** programmer support for PIC32C boards
- Improved test coverage across multiple peripherals

## Changelog

### New Board Support

The following boards are added in zSDK v4.4.0.0:

| Board | Description | Family |
|-------|-------------|--------|
| pic32ck_sg01_cult | PIC32CK SG01 Curiosity Ultra | PIC32CK SG/GC (MCU) |
| pic32ck_gc01_cult | PIC32CK GC01 Curiosity Ultra | PIC32CK SG/GC (MCU) |
| pic32cm_sg00_cpro | PIC32CM SG00 Curiosity Pro | PIC32CM SG/GC (MCU) |
| pic32cm_gc00_cpro | PIC32CM GC00 Curiosity Pro | PIC32CM SG/GC (MCU) |
| sam9x75_curiosity | SAM9X75 Curiosity | SAM9X7 (MPU) |

### New Drivers

| Driver | Description |
|--------|-------------|
| UDC | Added a USB Device Controller (UDC) driver for High-Speed USB (USBHS) peripherals on PIC32CK SG/GC families. |
| UDC | Added USB Device Controller (UDC) drivers for Full-Speed USB peripherals on SAM D5x/E5x, PIC32CX SG, and PIC32CK SG/GC families. |
| UDC | Added a USB Device Controller (UDC) driver for the SAMA7G5 High-Speed USB (UDPHS) peripheral. |
| SDHC | Added G1 SDHC driver for SAM D5x/E5x family |
| Ethernet (GMAC) | Added GMAC G1 Ethernet driver for Microchip devices |
| QSPI Flash | Added QSPI G1 flash driver for SAMA7G5 MPU devices |
| Flash NVMCTRL | Added NVMCTRL G2 flash driver for PIC32CM JH family |
| Flash NVMCTRL | Added NVMCTRL G3 flash driver for PIC32CZ CA80/CA90 |
| DMA | Added DMA G3 driver for PIC32CZ CA family |
| ADC | Added ADC G2 driver for PIC32CK SG/GC family |
| CAN | Added CAN G1 driver for PIC32CK SG/GC family |

### Driver Bug Fixes

- **SPI**: Fixed dummy size calculation in MCHP G1 SPI driver
- **Pinctrl**: Fixed PMUX nibble clearing before setting mux value in MCHP G1 pinctrl driver
- **DAC**: Fixed G1 DAC Kconfig defaults for SAM D5x/E5x devices
- **I2C**: Fixed I2C target mode in SERCOM G1 driver
- **RTC**: Updated SAM RTC driver to not hold spinlock during blocking calls
- **Serial**: Added parity check in serial config for SAM USART driver
- **Clock Control**: Fixed clock reinit after MCUboot for SAM D5x/E5x

### Driver Enhancements

#### The following drivers now support PIC32CK SG/GC family:

- **Clock**: Clock driver support
- **Counter**: Counter driver support
- **EIC**: Interrupt controller driver support
- **Ethernet**: Ethernet (GMAC) driver support
- **Flash**: NVMCTRL G3 flash driver support
- **GPIO**: GPIO driver support
- **HWInfo**: Hardware information driver support
- **I2C**: I2C driver support
- **Pinctrl**: Pinctrl driver support
- **PWM**: PWM support
- **RNG**: TRNG support
- **RTC**: RTC support
- **SDHC**: SDHC driver support
- **SPI**: SPI driver support
- **UART**: UART driver support
- **Watchdog**: Watchdog support

#### MPU Driver Support

- **DMA**: Cache coherence support for SAMA7G54-EK
- **PWM**: PWM API test support for SAMA7G54-EK
- **QSPI**: QSPI flash driver and device support for SAMA7G5
- **UART**: UART test support for SAMA7G5 and SAMA7D65

#### Ethernet Enhancements

- **OA TC6**: Implemented full duplex operation in TX and RX paths
- **OA TC6**: Made SPI tx/rx buffer size user configurable
- **LAN865x**: Moved OA TC6 specific code into OA TC6 library
- **LAN865x**: Enabled VLAN support

### Twister Tests

- **ADC**: Test support for PIC32CK GC
- **Comparator**: Fixed pin conflicts in overlays
- **Counter**: Test support for PIC32CK SG01 Curiosity Ultra
- **DMA**: Loop transfer test with cache coherence support for SAMA7G54-EK
- **Flash**: Flash test support
- **GPIO**: Test support for PIC32CM PL Family boards
- **I2C**: I2C target API test support for multiple boards
- **Pinctrl**: Test support for PIC32CK SG/GC, PIC32CM PL, PIC32CZ CA80/CA90, PIC32CM JH, PIC32CX SG41/SG61 Family boards
- **PWM**: Test support for PIC32CX SG41/SG61; fixed GPIO loopback pin conflicts
- **SPI**: Error case tests
- **UART**: Error handling tests for PIC32CK SG01 and PIC32CK GC01; support for PIC32CM PL Family

### Tools

- Added **MPLAB IPE** runner for west flash command
- Fixed multiple tools support with MPLAB IPE runner

### Samples

- Counter driver sample applications for PIC32CK SG01
- Filesystem sample overlay files
- Flash read/write sample application
- QSPI sample applications for Microchip devices

### zSDK Applications

- Added a sample application demonstrating a complete MCUboot boot chain with secure firmware upgrade (swap-using-move) support on Microchip PIC32CK Curiosity Ultra boards.
  - **Path:** `applications/mcuboot/mcuboot_basic`
  - **Documentation:** [`applications/mcuboot/mcuboot_basic/README.md`](../../applications/mcuboot/mcuboot_basic/README.md)

## Supported Boards

For detailed board documentation, refer to the [Microchip Boards Documentation](https://zephyr4microchip.github.io/doc-microchip-zephyr/boards/microchip/index.html).

## Known Issues

- The **USB Device Firmware Upgrade (DFU)** sample application does not function correctly when configured to use the **High-Speed USB (USBHS)** driver on **PIC32CK GC** and **PIC32CK SG** device families.
 

## How to Get the Code

```bash
west init -m https://github.com/Zephyr4Microchip/microchip-zsdk --mr v4.4.0.0 mchp_zephyrproject
cd mchp_zephyrproject
west update
```

