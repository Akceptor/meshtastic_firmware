#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
"""Let RadioLib recognise an LR1121 running Semtech's transceiver firmware image.

ExpressLRS flashes Semtech's LR1121 *transceiver* image (firmware type 0xF3, e.g. 0xF30104 --
see ExpressLRS src/lib/LR1121Driver/lr1121_transceiver_F30104.h) into the radio's internal
flash, and never restores the factory image. RadioLib's LR11x0::findChip() only accepts the
factory device byte 0x03 (or 0xDF for bootloader mode), so on any ex-ExpressLRS LR1121 board
begin() returns RADIOLIB_ERR_CHIP_NOT_FOUND (-2) even though the chip is healthy and SPI is
fine. The command set is otherwise identical, so accepting 0xF3 is enough.

RadioLib is consumed as an upstream release zip (see lib_deps in platformio.ini), so this is
applied to the downloaded copy at build time rather than carried as a source diff. Upstream
report: no fix available as of RadioLib 7.6.0.
"""
from os.path import isfile, join

Import("env")

MARKER = "RADIOLIB_LR11X0_DEVICE_LR1121_TRX"

OLD = "    if((info.device == ver) || (info.device == RADIOLIB_LR11X0_DEVICE_BOOT)) {"
NEW = """    // Patched by extra_scripts/lr11x0_accept_trx_firmware.py -- see that file for why.
#define RADIOLIB_LR11X0_DEVICE_LR1121_TRX (0xF3UL << 0)
    if((info.device == ver) || (info.device == RADIOLIB_LR11X0_DEVICE_BOOT) ||
       ((ver == RADIOLIB_LR11X0_DEVICE_LR1121) && (info.device == RADIOLIB_LR11X0_DEVICE_LR1121_TRX))) {"""

path = join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "RadioLib", "src", "modules", "LR11x0",
            "LR11x0.cpp")

if not isfile(path):
    print(f"lr11x0_accept_trx_firmware: {path} not found, skipping")
else:
    with open(path) as f:
        src = f.read()
    if MARKER in src:
        print("lr11x0_accept_trx_firmware: already patched")
    elif OLD not in src:
        raise SystemExit(f"lr11x0_accept_trx_firmware: findChip() match line not found in {path} -- "
                         "RadioLib changed, re-check the patch")
    else:
        with open(path, "w") as f:
            f.write(src.replace(OLD, NEW, 1))
        print("lr11x0_accept_trx_firmware: patched findChip() to accept LR1121 TRX firmware (0xF3)")
