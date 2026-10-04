"""Sequential device gates; independent source builds must finish first."""
import subprocess,sys,shutil
from pathlib import Path
from appliance_client import Client
ROOT=Path(__file__).resolve().parents[2]
def run(name,script,*args,python=None):
    print('GATE',name,flush=True)
    with (ROOT/f'.tools/hardware/{name}.log').open('w',encoding='utf8') as log:
        subprocess.run([str(python or sys.executable),str(ROOT/'experiments/hardware'/script),*args],stdout=log,stderr=subprocess.STDOUT,check=True,cwd=ROOT)
    print('GATE PASS',name,flush=True)
def main():
    c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True))
        print('FIXTURE',c.install(ROOT/'.pio/build/xiao-appliance-codec-test/firmware.bin'),flush=True)
        if not c.state()['cameraReady']:raise RuntimeError('Camera failed in fixture')
    finally:c.clear()
    for width,fps in ((320,10),(320,5),(640,5),(640,10)):
        folder=ROOT/'.tools/hardware/codec-probe'/f'{width}x{240 if width==320 else 480}-{fps}'
        if (folder/'result.json').exists():
            archive=folder/'before-sensor-fix';archive.mkdir(exist_ok=True)
            for name in ('result.json','source.avi','output.mp4'):
                if (folder/name).exists():shutil.copy2(folder/name,archive/name)
        run(f'codec-{width}-{fps}','appliance_codec_probe.py','--reuse','--keep-fixture','--width',str(width),'--fps',str(fps))
    run('codec-vga-long','appliance_codec_probe.py','--reuse','--keep-fixture','--width','640','--fps','10','--seconds','60')
    run('features-test','test_appliance_features.py')
    run('conversion-queue-test','test_conversion_queue.py','--keep-mp4')
    run('mp4-browser-test','test_mp4_browser.py',python=ROOT/'.tools/browser/venv/Scripts/python.exe')
    run('feature-browser-test','test_feature_browser.py',python=ROOT/'.tools/browser/venv/Scripts/python.exe')
    run('ble-final-test','appliance_ble_checks.py')
if __name__=='__main__':main()
