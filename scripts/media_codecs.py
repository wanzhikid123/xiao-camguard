"""Compile only the official S3 software encoder wrappers, link AAC and MP4."""
Import('env')
from pathlib import Path
import sys
import hashlib
import os
import subprocess

root = Path(env.subst('$PROJECT_DIR'))
sys.path.insert(0, str(root / 'scripts'))
from prepare_media_codecs import prepare
prepare()
h = root / '.tools/codec-source/esp-h264-component/esp_h264'
a = root / '.tools/codec-source/esp-adf-libs'
env.Append(CPPPATH=[str(h/p) for p in ('interface/include','sw/include','port/include','port/inc','sw/libs/openh264_inc','sw/src')]
    + [str(a/p) for p in ('esp_audio_codec/include','esp_audio_codec/include/encoder','esp_audio_codec/include/encoder/impl','esp_muxer/include','esp_muxer/include/impl')])
# These software-only archives use libc allocation for many small buffers.
# Arduino's small-allocation preference exhausts internal RAM before PSRAM.
# Rename only their undefined libc allocation references, leaving SDK, DMA,
# controller and FreeRTOS allocation unchanged. Keep pinned originals intact.
adapted=root / env.subst('$BUILD_DIR') / 'media-libs'
adapted.mkdir(parents=True,exist_ok=True)
toolchain=Path(env.PioPlatform().get_package_dir('toolchain-xtensa-esp-elf'))
objcopy=toolchain/'bin'/('xtensa-esp-elf-objcopy.exe' if os.name=='nt' else 'xtensa-esp-elf-objcopy')
names=[]
for original in (h/'sw/libs/esp32s3/libopenh264.a',a/'esp_audio_codec/lib/esp32s3/libesp_audio_codec.a',a/'esp_muxer/lib/esp32s3/libesp_muxer.a'):
    name='appliance_'+original.stem[3:];names.append(name)
    target=adapted/('lib'+name+'.a');stamp=target.with_suffix('.sha256')
    digest=hashlib.sha256(original.read_bytes()+b'codec-psram-allocator-v1').hexdigest()
    if not target.exists() or not stamp.exists() or stamp.read_text()!=digest:
        subprocess.run([str(objcopy),'--redefine-sym','malloc=appliance_codec_malloc',
            '--redefine-sym','calloc=appliance_codec_calloc','--redefine-sym','realloc=appliance_codec_realloc',
            str(original),str(target)],check=True)
        stamp.write_text(digest,encoding='ascii')
env.Append(LIBPATH=[str(adapted)],LIBS=names)
codec_env = env.Clone()
codec_env.Append(CPPDEFINES=['HAVE_ESP32S3'])
wrappers = codec_env.BuildLibrary('$BUILD_DIR/media-encoder', str(h), src_filter='-<*> '
    '+<interface/src/esp_h264_enc_single.c> +<interface/src/esp_h264_enc_param.c> '
    '+<sw/src/esp_h264_enc_single_sw.c> +<sw/src/esp_h264_enc_sw_param.c> '
    '+<sw/src/h264_color_convert.c> +<sw/src/asm/esp32s3/h264_color_convert.S> '
    '+<port/src/esp_h264_alloc.c> +<port/src/esp_h264_cache.c>')
env.Append(LIBS=[wrappers])
