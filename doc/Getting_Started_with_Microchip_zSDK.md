# Getting Started with Microchip zSDK

## Overview

This guide provides step-by-step instructions for setting up the Microchip zSDK on a development machine. The Microchip zSDK enables you to build, flash, and debug Zephyr-based applications for Microchip microcontrollers.

## Installation Steps

### Step 1: Install Dependencies

If you have already set up Zephyr on your system, you may skip dependency installation.

Install the required system dependencies for your operating system by following only the "Install Dependencies" section from the Zephyr Getting Started Guide:
https://docs.zephyrproject.org/latest/develop/getting_started/index.html#install-dependencies

Skip all other sections in the Zephyr guide, they are covered in the steps below.

### Step 2: Create Virtual Environment

Create and activate a Python virtual environment for the Microchip zSDK:

#### Linux

```bash
python3 -m venv ~/mchp_zephyrproject/.venv
source ~/mchp_zephyrproject/.venv/bin/activate
```

#### Windows

```bat
cd %HOMEPATH%
python -m venv mchp_zephyrproject\.venv
mchp_zephyrproject\.venv\Scripts\activate.bat
```

> Always make sure the virtual environment is activated while working with Microchip zSDK.

### Step 3: Install West

Install the `west` meta-tool used for Zephyr repository management and building:

```bash
pip install west
```

### Step 4: Get the Microchip Zephyr SDK Source

Initialize and clone the Microchip Zephyr SDK repositories:

```bash
west init -m https://github.com/Zephyr4Microchip/microchip-zsdk mchp_zephyrproject
cd mchp_zephyrproject
west update
```

> The `west update` command may take several minutes to complete as it clones multiple repositories.

### Step 5: Install Python Dependencies

Install additional Python packages required by Zephyr:

```bash
west packages pip --install
```

### Step 6: Export CMake Package

Export the Zephyr CMake package so applications can find it:

```bash
west zephyr-export
```

### Step 7: Install Zephyr SDK (Toolchain)

Install the Zephyr SDK which includes compilers and tools for all supported architectures:

> If the Zephyr SDK is already installed on your system, you may skip this step.

```bash
cd zephyr
west sdk install
```

## Installation Complete

Congratulations! The Microchip zSDK installation and configuration is now complete.

## Testing the Installation

Verify your setup by building and flashing a sample application to a supported Microchip board.

### Build the Blinky Sample

#### Linux

```bash
cd ~/mchp_zephyrproject/zephyr
west build -p always -b pic32cx_sg41_cult samples/basic/blinky
```

#### Windows

```bat
cd %HOMEPATH%\mchp_zephyrproject\zephyr
west build -p always -b pic32cx_sg41_cult samples\basic\blinky
```

To list supported boards:

```bash
west boards
```

### Flash the Sample

Connect the board to your PC and flash using the following command:

```bash
west flash
```
