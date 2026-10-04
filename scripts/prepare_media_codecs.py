"""Fetch exact upstream ESP32-S3 encoder/muxer dependencies into ignored .tools.

No SDK upgrades, global installs, or binary assets embedded in the source repo.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    ('esp-h264-component', 'https://github.com/espressif/esp-h264-component.git',
     'afff884d9fa6674658faaf6b2003e7969be287ef', ['esp_h264']),
    ('esp-adf-libs', 'https://github.com/espressif/esp-adf-libs.git',
     'a1c9747e62e9f78444570ce6dfb0c4143de98393', ['esp_audio_codec', 'esp_muxer']),
)


def prepare():
    for name, url, commit, directories in SOURCES:
        path = ROOT / '.tools' / 'codec-source' / name
        if not (path / '.git').exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(['git', 'clone', '--filter=blob:none', '--no-checkout', url, str(path)], check=True)
        def git(*args):
            return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()
        if git('rev-parse', 'HEAD') != commit or not all((path / directory).exists() for directory in directories):
            subprocess.run(['git', '-C', str(path), 'fetch', '--depth', '1', 'origin', commit], check=True)
            subprocess.run(['git', '-C', str(path), 'sparse-checkout', 'set', *directories], check=True)
            subprocess.run(['git', '-C', str(path), 'checkout', '--detach', commit], check=True)
        print(name, commit, flush=True)


if __name__ == '__main__':
    prepare()
