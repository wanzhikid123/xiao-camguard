"""Launch repository-local PlatformIO without changing global Python packages."""
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = None
for candidate in (ROOT / ".tools" / "python", ROOT / ".tools" / "pydeps"):
    try:
        # A managed session may see a path without being able to read it.
        for entry in ("__init__.py", "__main__.py"):
            with (candidate / "platformio" / entry).open("rb") as package_file:
                package_file.read(1)
    except OSError:
        continue
    PACKAGES = candidate
    break
if PACKAGES is None:
    raise SystemExit("Run: python -m pip install --target .tools/python -r requirements-tools.txt")
sys.path.insert(0, str(PACKAGES))
os.environ["PYTHONPATH"] = str(PACKAGES)
os.environ["PLATFORMIO_CORE_DIR"] = str(ROOT / ".tools" / "platformio")
os.environ["PYTHONIOENCODING"] = "utf-8"
for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, "reconfigure"):
        stream.reconfigure(encoding="utf-8", errors="replace")
os.chdir(ROOT)

from platformio.__main__ import main

if __name__ == "__main__":
    raise SystemExit(main())
