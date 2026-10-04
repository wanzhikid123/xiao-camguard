"""Embed only the two public upstream UI assets in an isolated provisioning utility."""
Import("env")
import hashlib
from pathlib import Path

root = Path(env.subst("$PROJECT_DIR")).resolve().parents[1]
build = Path(env.subst("$BUILD_DIR"))
build.mkdir(parents=True, exist_ok=True)
header = ["// Generated public UI assets; no configuration or credentials."]
rows = []
for index, name in enumerate(("common.js", "MJPEG2SD.htm")):
    data = (root / "data" / name).read_bytes()
    values = ",".join(str(byte) for byte in data)
    header.append(f"static const uint8_t asset_{index}[] = {{{values}}};")
    rows.append(f'{{"/data/{name}", asset_{index}, sizeof(asset_{index}), "{hashlib.md5(data).hexdigest()}"}}')
header.append("static const SeedAsset assets[] = {" + ",".join(rows) + "};")
(build / "seed_assets.h").write_text("\n".join(header), encoding="utf-8")
env.Append(CPPPATH=[str(build)])
