"""PlatformIO post-build hook: fail if firmware consumes more than 80% of an OTA slot."""
Import("env")
import importlib.util
import hashlib
from pathlib import Path

root = Path(env.subst("$PROJECT_DIR"))
spec = importlib.util.spec_from_file_location("check_flash_budget", root / "scripts/check_flash_budget.py")
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


def check_budget(source, target, env):
    binary = Path(str(target[0]))
    _, slot = budget.inspect_partitions(root / "partitions/xiao_8mb_ota.csv")
    report = budget.firmware_budget(binary, slot)
    print(f"OTA budget: {report['firmware_bytes']} / {slot} bytes ({report['used_percent']}%), limit 80%")
    if not report["passed"]:
        raise RuntimeError("Firmware exceeds release OTA size budget")
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    binary.with_suffix(binary.suffix + ".sha256").write_text(digest + "  " + binary.name + "\n", encoding="ascii")
    print(f"OTA companion digest: {binary.name}.sha256")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_budget)
