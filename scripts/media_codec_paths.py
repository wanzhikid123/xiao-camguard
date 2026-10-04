"""Configure dependency includes before PlatformIO clones compilation envs."""
Import('env')
from pathlib import Path
import sys
root = Path(env.subst('$PROJECT_DIR'))
sys.path.insert(0, str(root / 'scripts'))
from prepare_media_codecs import prepare
prepare()
h = root / '.tools/codec-source/esp-h264-component/esp_h264'
a = root / '.tools/codec-source/esp-adf-libs'
env.Append(CPPPATH=[str(h/p) for p in ('interface/include','sw/include','port/include','port/inc','sw/libs/openh264_inc','sw/src')]
    + [str(a/p) for p in ('esp_audio_codec/include','esp_audio_codec/include/encoder','esp_audio_codec/include/encoder/impl','esp_muxer/include','esp_muxer/include/impl')])
