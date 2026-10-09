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
# JPEG runs with the live camera, unlike the idle H.264 worker. Its preferred
# internal workspaces exhausted DRAM needed by TCP responses. Adapt only this
# pinned SDK archive's private allocation calls; never change SDK-wide policy.
sdk=Path(env.PioPlatform().get_package_dir('framework-arduinoespressif32-libs'))
jpeg=sdk/env.BoardConfig().get('build.mcu')/'lib/libesp_new_jpeg.a'
jpeg_target=adapted/'libappliance_jpeg.a'
jpeg_stamp=jpeg_target.with_suffix('.sha256')
jpeg_digest=hashlib.sha256(jpeg.read_bytes()+b'jpeg-psram-allocator-v1').hexdigest()
if not jpeg_target.exists() or not jpeg_stamp.exists() or jpeg_stamp.read_text()!=jpeg_digest:
    subprocess.run([str(objcopy),'--redefine-sym','heap_caps_calloc_prefer=appliance_jpeg_calloc_prefer',
        '--redefine-sym','heap_caps_aligned_calloc=appliance_jpeg_aligned_calloc',
        str(jpeg),str(jpeg_target)],check=True)
    jpeg_stamp.write_text(jpeg_digest,encoding='ascii')
# Select the adapted component before the SDK's unchanged copy. Linking the
# entire component together preserves the encoder/decoder's matching ABI.
env.Prepend(LIBS=['appliance_jpeg'])
codec_env = env.Clone()
codec_env.Append(CPPDEFINES=['HAVE_ESP32S3'])
wrappers = codec_env.BuildLibrary('$BUILD_DIR/media-encoder', str(h), src_filter='-<*> '
    '+<interface/src/esp_h264_enc_single.c> +<interface/src/esp_h264_enc_param.c> '
    '+<sw/src/esp_h264_enc_single_sw.c> +<sw/src/esp_h264_enc_sw_param.c> '
    '+<sw/src/h264_color_convert.c> +<sw/src/asm/esp32s3/h264_color_convert.S> '
    '+<port/src/esp_h264_alloc.c> +<port/src/esp_h264_cache.c>')
env.Append(LIBS=[wrappers])
