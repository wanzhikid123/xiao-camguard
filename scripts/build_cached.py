"""Build the baseline with installed tools, without the Core download stage.

This is a build-only fallback for restricted/offline sessions after normal
PlatformIO dependency installation. It never flashes, erases or updates tools.
"""
import argparse
import base64
import os
import subprocess
import sys

from pio import ROOT, PACKAGES


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    core = ROOT / ".tools/platformio"
    scons = core / "packages/tool-scons/scons.py"
    platform_builder = core / "platforms/espressif32/builder/main.py"
    pio_builder = PACKAGES / "platformio/builder/main.py"
    for required in (scons, platform_builder, pio_builder):
        if not required.is_file():
            raise SystemExit("Install tools first: python scripts/pio.py run -e xiao-baseline")

    process_env = os.environ.copy()
    process_env["PLATFORMIO_OFFLINE"] = "1"
    # The copied Core fallback may include another esptool module. Ensure the
    # platform's package check sees the actual installed, pinned tool source.
    process_env["PYTHONPATH"] = os.pathsep.join([
        str(core / "packages/tool-esptoolpy"), str(PACKAGES),
    ])

    def encode(value):
        return base64.urlsafe_b64encode(str(value).encode("utf-8")).decode("ascii")

    command = [sys.executable, str(scons), "-Q", "--jobs", str(args.jobs),
               "--sconstruct", str(pio_builder), "PIOVERBOSE=0", "ISATTY=0",
               "PIOENV=" + encode("xiao-baseline"),
               "PROJECT_CONFIG=" + encode(ROOT / "platformio.ini"),
               "BUILD_SCRIPT=" + encode(platform_builder)]
    return subprocess.run(command, cwd=ROOT, env=process_env, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
