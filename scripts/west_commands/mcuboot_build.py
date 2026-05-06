# Copyright (c) 2026 Microchip Technology Inc.
# SPDX-License-Identifier: Apache-2.0

"""West command to build MCUboot bootloader and application images."""

import argparse
import os
import subprocess
import sys
from pathlib import Path

import yaml
from west.commands import WestCommand
from west import log


def find_objcopy():
    """Find arm objcopy from Zephyr SDK or PATH."""
    exe_suffix = '.exe' if sys.platform == 'win32' else ''

    def _check(path):
        """Check path with and without exe suffix."""
        if path.exists():
            return str(path)
        with_exe = path.with_name(path.name + exe_suffix)
        if exe_suffix and with_exe.exists():
            return str(with_exe)
        return None

    # Check ZEPHYR_SDK_INSTALL_DIR first
    sdk_dir = os.environ.get('ZEPHYR_SDK_INSTALL_DIR')
    if sdk_dir:
        result = _check(Path(sdk_dir) / 'gnu' / 'arm-zephyr-eabi' / 'bin' / 'arm-zephyr-eabi-objcopy')
        if result:
            return result

    # Check common SDK locations
    home = Path.home()
    for sdk_path in sorted(home.glob('zephyr-sdk-*'), reverse=True):
        result = _check(sdk_path / 'gnu' / 'arm-zephyr-eabi' / 'bin' / 'arm-zephyr-eabi-objcopy')
        if result:
            return result

    # Fall back to PATH
    for name in ['arm-zephyr-eabi-objcopy', 'arm-none-eabi-objcopy']:
        for path_dir in os.environ.get('PATH', '').split(os.pathsep):
            result = _check(Path(path_dir) / name)
            if result:
                return result

    return None

MCUBOOT_BUILD_DESCRIPTION = '''\
Build MCUboot bootloader with signed application images.

This command builds:
  1. MCUboot bootloader
  2. Application v1 (signed for slot0)
  3. Application v2 (signed for slot1)

The command can be run from anywhere within the west workspace.
'''

MCUBOOT_BUILD_EPILOG = '''\
Examples
--------

Build for a specific board:

    west mcuboot-build -b pic32ck_gc01_cult

Build for a specific board (verbose):

    west mcuboot-build -b pic32ck_sg01_cult --verbose

Build and flash to the target:

    west mcuboot-build -b pic32ck_sg01_cult --flash

Build using a custom application directory:

    west mcuboot-build -b pic32ck_sg01_cult -a path/to/my_mcuboot_app

List available boards:

    west mcuboot-build --list-boards

Clean build (pristine):

    west mcuboot-build -b pic32ck_sg01_cult --pristine

Adding a New Board
------------------

To add support for a new board:

1. Add board entry to boards.yaml with required parameters:
   - jlink_device: JLink device identifier
   - slot_size: Slot size in hex (e.g., 0xFC000)
   - slot0_addr: Slot0 address in hex
   - slot1_addr: Slot1 address in hex
   - header_size: Image header size in hex
   - align: Image alignment

2. Add board-specific settings to app_v1/sysbuild/mcuboot.conf
   (and app_v2/sysbuild/mcuboot.conf) if the new board needs
   different MCUboot overrides.

3. Optionally create board-specific app config files:
   - app_v1/boards/<board>.conf
   - app_v2/boards/<board>.conf
'''


class McubootBuild(WestCommand):
    """West command to build MCUboot and application images."""

    def __init__(self):
        super().__init__(
            'mcuboot-build',
            'build MCUboot bootloader with signed application images',
            description=MCUBOOT_BUILD_DESCRIPTION,
            accepts_unknown_args=False)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog=MCUBOOT_BUILD_EPILOG)

        parser.add_argument(
            '-b', '--board',
            metavar='BOARD',
            help='target board name (required for build)')

        parser.add_argument(
            '-a', '--app',
            metavar='PATH',
            help='path to mcuboot application directory '
                 '(default: microchip-zsdk/applications/mcuboot/mcuboot_basic)')

        parser.add_argument(
            '--flash',
            action='store_true',
            help='flash images to target after building')

        parser.add_argument(
            '-p', '--pristine',
            action='store_true',
            help='perform a pristine (clean) build')

        parser.add_argument(
            '--list-boards',
            action='store_true',
            help='list available boards from boards.yaml and exit')

        parser.add_argument(
            '--build-dir',
            metavar='PATH',
            help='base directory for build output '
                 '(default: <zephyr-dir>)')

        parser.add_argument(
            '-v', '--verbose',
            action='store_true',
            help='enable verbose output')

        return parser

    def do_run(self, args, unknown_args):
        self.args = args

        # Get workspace paths
        self.workspace = Path(self.topdir)
        self.zsdk_dir = self.workspace / 'microchip-zsdk'
        self.zephyr_dir = self.workspace / 'zephyr'
        self.boot_dir = self.workspace / 'bootloader' / 'mcuboot'

        # Resolve app directory
        if args.app:
            self.app_dir = Path(args.app)
            if not self.app_dir.is_absolute():
                self.app_dir = Path.cwd() / self.app_dir
        else:
            self.app_dir = self.zsdk_dir / 'applications' / 'mcuboot' / 'mcuboot_basic'

        # Resolve build directory
        if args.build_dir:
            self.build_base = Path(args.build_dir)
            if not self.build_base.is_absolute():
                self.build_base = Path.cwd() / self.build_base
        else:
            self.build_base = self.zephyr_dir

        # Validate paths
        self._validate_paths()

        # Load board configuration
        self.boards_yaml = self.app_dir / 'boards.yaml'
        self.boards_config = self._load_boards_yaml()

        # Handle --list-boards
        if args.list_boards:
            self._list_boards()
            return

        # Board is required for build
        if not args.board:
            log.die("Board is required. Use -b <board> or --list-boards to see available boards")

        # Find objcopy (only needed for actual builds)
        self.objcopy = find_objcopy()
        if not self.objcopy:
            log.die("Could not find arm objcopy. Please install Zephyr SDK or set ZEPHYR_SDK_INSTALL_DIR")

        board = args.board
        if board not in self.boards_config.get('boards', {}):
            log.die(f"Board '{board}' not found in boards.yaml\n"
                    f"Run 'west mcuboot-build --list-boards' to see available boards")

        self.board = board
        self.board_config = self.boards_config['boards'][board]

        # Validate required board parameters
        self._validate_board_params()

        # Validate board files exist
        self._validate_board_files()

        # Print configuration
        self._print_config()

        # Build
        self._build_mcuboot()
        self._build_app('app_v1', '1.0.0', 'slot0')
        self._build_app('app_v2', '2.0.0', 'slot1')

        # Flash if requested
        if args.flash:
            self._flash_images()

        log.inf('\nBuild completed successfully!')

    def _validate_paths(self):
        """Validate required paths exist."""
        if not self.app_dir.exists():
            log.die(f"Application directory not found: {self.app_dir}")

        if not self.zephyr_dir.exists():
            log.die(f"Zephyr directory not found: {self.zephyr_dir}")

        if not self.boot_dir.exists():
            log.die(f"MCUboot directory not found: {self.boot_dir}")

    def _validate_board_params(self):
        """Validate required board parameters are present in boards.yaml."""
        required_params = [
            'jlink_device', 'slot_size', 'slot0_addr',
            'slot1_addr', 'header_size', 'align',
        ]
        missing = [p for p in required_params if not self.board_config.get(p)]
        if missing:
            log.die(f"Board '{self.board}' is missing required parameters in "
                    f"boards.yaml: {', '.join(missing)}")

    def _load_boards_yaml(self):
        """Load and parse boards.yaml."""
        if not self.boards_yaml.exists():
            log.die(f"boards.yaml not found at: {self.boards_yaml}")

        with open(self.boards_yaml, 'r') as f:
            try:
                return yaml.safe_load(f)
            except yaml.YAMLError as e:
                log.die(f"Failed to parse boards.yaml: {e}")

    def _list_boards(self):
        """Print available boards and their configuration."""
        boards = self.boards_config.get('boards', {})
        if not boards:
            log.inf("No boards defined in boards.yaml")
            return

        log.inf("Available boards:")
        log.inf("")
        for name, config in boards.items():
            jlink = config.get('jlink_device', 'N/A')
            slot_size = config.get('slot_size', 'N/A')
            log.inf(f"  {name}")
            log.inf(f"    JLink device: {jlink}")
            log.inf(f"    Slot size:    {slot_size}")
            log.inf("")

    def _validate_board_files(self):
        """Validate MCUboot configuration files exist.

        The standard approach uses sysbuild/mcuboot.conf (merge fragment) and
        sysbuild/mcuboot.overlay from app_v1. These same files are passed as
        -DOVERLAY_CONFIG and -DDTC_OVERLAY_FILE to the standalone MCUboot build,
        ensuring identical configuration for both build methods.
        """
        required_files = [
            self.app_dir / 'app_v1' / 'sysbuild' / 'mcuboot.conf',
            self.app_dir / 'app_v1' / 'sysbuild' / 'mcuboot.overlay',
        ]

        optional_files = [
            self.app_dir / 'app_v1' / 'boards' / f'{self.board}.conf',
            self.app_dir / 'app_v2' / 'boards' / f'{self.board}.conf',
        ]

        missing_required = [f for f in required_files if not f.exists()]
        if missing_required:
            log.err("Missing required MCUboot configuration files:")
            for f in missing_required:
                log.err(f"  {f}")
            log.die("Please create the missing files before building")

        missing_optional = [f for f in optional_files if not f.exists()]
        if missing_optional:
            log.wrn("Missing optional board configuration files:")
            for f in missing_optional:
                log.wrn(f"  {f}")
            log.wrn("Build will proceed using prj.conf defaults")

    def _print_config(self):
        """Print build configuration."""
        log.inf("")
        log.inf("=" * 70)
        log.inf(f"  Building MCUboot Demo for {self.board}")
        log.inf("=" * 70)
        log.inf("")
        log.inf(f"Board:       {self.board}")
        log.inf(f"JLink:       {self.board_config.get('jlink_device', 'N/A')}")
        log.inf(f"Slot size:   {self.board_config.get('slot_size', 'N/A')}")
        log.inf(f"Slot0:       {self.board_config.get('slot0_addr', 'N/A')}")
        log.inf(f"Slot1:       {self.board_config.get('slot1_addr', 'N/A')}")
        log.inf(f"Header size: {self.board_config.get('header_size', 'N/A')}")
        log.inf(f"Align:       {self.board_config.get('align', 'N/A')}")
        log.inf(f"Workspace:   {self.workspace}")
        log.inf(f"App dir:     {self.app_dir}")
        log.inf("")

    def _cmake_path(self, path):
        """Convert path to forward slashes for CMake compatibility."""
        return str(path).replace('\\', '/')

    def _run_command(self, cmd, cwd=None, check=True):
        """Run a command and handle errors."""
        if self.args.verbose:
            log.inf(f"Running: {' '.join(str(c) for c in cmd)}")

        try:
            result = subprocess.run(
                cmd,
                cwd=cwd or self.zephyr_dir,
                check=check,
                capture_output=not self.args.verbose,
                text=True
            )
            return result
        except subprocess.CalledProcessError as e:
            log.err(f"Command failed with exit code {e.returncode}")
            if e.stdout:
                log.err(f"stdout: {e.stdout}")
            if e.stderr:
                log.err(f"stderr: {e.stderr}")
            raise

    def _build_mcuboot(self):
        """Build MCUboot bootloader.

        Uses -DOVERLAY_CONFIG (merge) instead of -DCONF_FILE (replace) so that
        MCUboot's own prj.conf is preserved as the base configuration. The
        sysbuild/mcuboot.conf fragment adds only the settings that upstream
        does not provide. This matches how sysbuild auto-discovers the same
        file as EXTRA_CONF_FILE.
        """
        log.inf("[1/3] Building MCUboot...")

        build_dir = self.build_base / 'build_mcuboot'
        overlay_conf = self.app_dir / 'app_v1' / 'sysbuild' / 'mcuboot.conf'
        dtc_overlay = self.app_dir / 'app_v1' / 'sysbuild' / 'mcuboot.overlay'

        cmd = [
            'west', 'build',
            '-b', self.board,
            '-d', self._cmake_path(build_dir),
            self._cmake_path(self.boot_dir / 'boot' / 'zephyr'),
        ]

        if self.args.pristine:
            cmd.append('--pristine')

        cmd.extend([
            '--',
            f'-DOVERLAY_CONFIG={self._cmake_path(overlay_conf)}',
            f'-DDTC_OVERLAY_FILE={self._cmake_path(dtc_overlay)}',
        ])

        self._run_command(cmd)
        log.inf("  MCUboot build complete")

    def _build_app(self, app_name, version, slot):
        """Build and sign an application image."""
        step = '2/3' if app_name == 'app_v1' else '3/3'
        slot_addr_key = 'slot0_addr' if slot == 'slot0' else 'slot1_addr'
        slot_addr = self.board_config.get(slot_addr_key)

        log.inf(f"[{step}] Building {app_name}...")

        build_dir = self.build_base / f'build_{app_name}'
        app_src = self.app_dir / app_name

        # Build
        cmd = [
            'west', 'build',
            '-b', self.board,
            '-d', self._cmake_path(build_dir),
            self._cmake_path(app_src),
        ]

        if self.args.pristine:
            cmd.append('--pristine')

        self._run_command(cmd)

        # Sign
        log.inf(f"  Signing {app_name} for {slot} ({slot_addr}) [ECDSA-P256]...")

        imgtool = self.boot_dir / 'scripts' / 'imgtool.py'
        key = self.boot_dir / 'root-ec-p256.pem'
        input_bin = build_dir / 'zephyr' / 'zephyr.bin'
        output_bin = build_dir / 'zephyr' / 'zephyr.signed.bin'

        sign_cmd = [
            sys.executable, self._cmake_path(imgtool), 'sign',
            '--key', self._cmake_path(key),
            '--header-size', self.board_config.get('header_size', '0x400'),
            '--align', str(self.board_config.get('align', '8')),
            '--version', version,
            '--slot-size', self.board_config.get('slot_size'),
            self._cmake_path(input_bin),
            self._cmake_path(output_bin),
        ]

        self._run_command(sign_cmd)

        # Convert to hex with correct load address
        log.inf(f"  Converting to hex with load address {slot_addr}...")

        output_hex = build_dir / 'zephyr' / f'zephyr.signed.{slot}.hex'

        objcopy_cmd = [
            self.objcopy,
            '-I', 'binary',
            '-O', 'ihex',
            f'--change-addresses={slot_addr}',
            self._cmake_path(output_bin),
            self._cmake_path(output_hex),
        ]

        self._run_command(objcopy_cmd)
        log.inf(f"  {app_name} build complete")

    def _flash_images(self):
        """Flash images to target using JLink."""
        log.inf("")
        log.inf("[Flash] Flashing images to target...")

        jlink_device = self.board_config.get('jlink_device')
        if not jlink_device:
            log.die("JLink device not specified in boards.yaml")

        # Determine JLink executable name based on platform
        jlink_exe = 'JLink.exe' if sys.platform == 'win32' else 'JLinkExe'

        # Create JLink command file
        jlink_script = self.build_base / 'flash.jlink'
        mcuboot_hex = self.build_base / 'build_mcuboot' / 'zephyr' / 'zephyr.hex'
        app_v1_hex = self.build_base / 'build_app_v1' / 'zephyr' / 'zephyr.signed.slot0.hex'
        app_v2_hex = self.build_base / 'build_app_v2' / 'zephyr' / 'zephyr.signed.slot1.hex'

        jlink_commands = f'''\
r
h
erase
loadfile {self._cmake_path(mcuboot_hex)}
loadfile {self._cmake_path(app_v1_hex)}
loadfile {self._cmake_path(app_v2_hex)}
r
g
q
'''

        with open(jlink_script, 'w') as f:
            f.write(jlink_commands)

        flash_cmd = [
            jlink_exe,
            '-device', jlink_device,
            '-if', 'SWD',
            '-speed', '4000',
            '-autoconnect', '1',
            '-CommandFile', str(jlink_script),
        ]

        self._run_command(flash_cmd)
        log.inf("  Flash complete")
