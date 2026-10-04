"""Use esptool's Python entry point with the pinned pioarduino platform.

Windows application control can allow Python while rejecting the generated
esptool.exe console launcher. Keep the installed tool/version and use its
official module entry point for image generation and serial upload.
"""
Import("env")

import os
import subprocess
from pathlib import Path
from SCons.Script import COMMAND_LINE_TARGETS


def merge_factory_image(source, target, env):
    firmware = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin"))
    factory = firmware.with_name(firmware.stem + ".factory.bin")
    command = [
        env.subst("$PYTHONEXE"), "-X", "utf8", "-I", "-m", "esptool",
        "--chip", env.BoardConfig().get("build.mcu"), "merge-bin",
        "-o", str(factory),
        "--flash-mode", env["__get_board_flash_mode"](env),
        "--flash-freq", env["__get_board_f_image"](env),
        "--flash-size", env.BoardConfig().get("upload.flash_size", "4MB"),
    ]
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        command.extend([str(offset), env.subst(image)])
    command.extend([env.subst("$ESP32_APP_OFFSET") or "0x10000", str(firmware)])
    # Propagate failures: a missing factory image must not look like success.
    return subprocess.run(command, check=False).returncode


if os.name == "nt":
    # -I keeps a PlatformIO PYTHONPATH from shadowing the pinned venv package.
    module_command = '"$PYTHONEXE" -X utf8 -I -m esptool'
    env.Replace(OBJCOPY=module_command, ERASETOOL=module_command)
    if Path(env.subst("$UPLOADER").strip('"')).name.lower() == "esptool.exe":
        env.Replace(UPLOADER=module_command)

    # This platform's factory hook launches esptool.exe directly instead of
    # using OBJCOPY. Replace only that hook; preserve size/budget actions.
    # SCons 4.11.1 and the platform are pinned. Fail visibly if the hook changes.
    if not set(COMMAND_LINE_TARGETS) & {"nobuild", "buildfs", "uploadfs", "uploadfsota"}:
        executor = env.File("$BUILD_DIR/${PROGNAME}.bin").get_executor()
        def is_factory_action(action):
            return getattr(getattr(action, "execfunction", None), "__name__", "") == "esp32_create_combined_bin"

        matches = [index for index, action in enumerate(executor.post_actions)
                   if is_factory_action(action)]
        delayed = env.get("_PIO_DELAYED_ACTIONS", [])
        pending = [index for index, (_, _, action) in enumerate(delayed)
                   if is_factory_action(action)]
        if len(matches) + len(pending) != 1:
            raise RuntimeError("Pinned pioarduino factory hook changed; review esptool_module.py")
        replacement = env.VerboseAction(
            merge_factory_image, "Creating factory image with python -m esptool")
        if matches:
            executor.post_actions[matches[0]] = replacement
        else:
            handler, nodes, _ = delayed[pending[0]]
            delayed[pending[0]] = (handler, nodes, replacement)
