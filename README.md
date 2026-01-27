# Microchip ZSDK for Zephyr

This repository contains the **Microchip SDK for Zephyr (ZSDK)**, which is Microchip’s primary downstream enablement for the Zephyr RTOS.

Microchip is a [Silver Member][project-members] of the Zephyr Project and is committed to providing **upstream support for Microchip hardware platforms**. In addition to upstream contributions, Microchip provides this downstream [manifest repository][west-manifest] to deliver:

- Support for new hardware not yet available upstream
- Early access to features under development
- Integration of components that cannot be upstreamed (for example, content with non–open-source-compatible licenses)
- Additional release validation and quality assurance for Microchip platforms

Microchip follows an **upstream-first development methodology**. The downstream ZSDK is based on upstream Zephyr stable releases, while minimizing the number of downstream-only patches.

---

[project-members]: https://zephyrproject.org/project-members/
[boards]: https://docs.zephyrproject.org/latest/boards/microchip/index.html
[west]: https://docs.zephyrproject.org/latest/develop/west/index.html
[west-manifest]: https://docs.zephyrproject.org/latest/develop/west/manifest.html

---

## Structure

This repository is the top-level [manifest repository][west-manifest] for the **Microchip ZSDK for Zephyr**. The SDK uses the [West][west] tool to fetch and organize all required repositories.

The [manifest file](./west.yml) defines which repositories are included and the revision used for each. Important repositories include:

- **[mchp-zsdk][repo-zsdk]**
  The manifest repository for Microchip ZSDK. This serves as the entry point for the SDK and references a mix of:
  - Upstream Zephyr repositories
  - Microchip downstream forks
  - Additional Microchip-specific modules

- **[mchp-zephyr][repo-zephyr]**
  Microchip’s downstream fork of the Zephyr RTOS repository. It is kept aligned with upstream Zephyr releases and may contain additional patches that are still under upstream review.

- **[hal_microchip][repo-hal-microchip]**
  Microchip’s HAL repository used by Zephyr. This includes hardware abstraction layers, peripheral drivers, and device support components derived from Microchip firmware packages.

- **mchp-wireless-ble**
  Microchip wireless connectivity components (such as BLE support) used alongside Zephyr.

---

[repo-zsdk]: https://github.com/Zephyr4Microchip/mchp-zsdk
[repo-zephyr]: https://github.com/Zephyr4Microchip/mchp-zephyr
[repo-hal-microchip]: https://github.com/Zephyr4Microchip/mchp-hal

---

## Workspace Layout

A typical workspace created using this manifest looks like:

```
workspace/
├── .west                      # West configuration
├── modules/
│   └── hal/
│       ├── cmsis_6/           # Upstream CMSIS 6 repository
│       └── hal-microhcip/     # Microchip downstream fork of hal_microchip
├── zephyr/                    # Microchip downstream fork of Zephyr
├── mchp-zsdk/                 # Microchip SDK for Zephyr manifest repository
├── zephyr-wireless/           # Micorhcip zephyr wireless
└── wireless-ble/              # Microchip wireless ble repository

```


Additional modules may be included as defined in the manifest.

---

## Getting Started

To get started with the **Microchip ZSDK for Zephyr**, follow the official
[Zephyr Project Getting Started Guide][zephyr-getting-started].

Instead of initializing your workspace with the upstream Zephyr manifest, use the Microchip ZSDK manifest:

```bash
west init -m https://github.com/Zephyr4Microchip/mchp-zsdk microchip_zephyr
cd microchip_zephyr
west update
west blobs fetch hal_microchip
```

It is also possible to clone the repository manually, and use `west init -l` to
perform initialization from local sources.

The Getting Started Guide covers setting up the build environment, as well as
building and flashing an example.

To use Zephyr with Silicon Labs devices, certain pre-built libraries are
required for the radio. The `west blobs fetch` command downloads these
libraries.

[zephyr-getting-started]: https://docs.zephyrproject.org/latest/develop/getting_started/index.html
