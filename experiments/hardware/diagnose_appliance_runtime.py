"""Repeat the real runtime symptom; retain only allowlisted diagnostic output."""
from pathlib import Path
import argparse
import json
import threading
import time
import serial
from serial_markers import open_console
from appliance_client import Client

ROOT=Path(__file__).resolve().parents[2]
MARKERS=(b'[CODEC_DIAG]',b'abort() was called',b'Crash loop',b'Camera init error',b'Failed to get camera',b'Camera tasks unavailable',b'Insufficient memory',
         b'Native BLE',b'Management services',b'Failed to register',b'OTA boot health',b'Guru Meditation',b'Backtrace:',
         b'stack canary',b'Stack canary',b'Stack protection',b'CORRUPT HEAP',b'assert failed')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--restart',action='store_true');args=parser.parse_args()
    stop=threading.Event();report={'markers':[],'checks':[]}
    def console():
        while not stop.is_set():
            try:
                with open_console() as p:
                    while not stop.is_set():
                        line=p.readline()
                        if any(marker in line for marker in MARKERS):
                            text=line.decode(errors='replace').strip();report['markers'].append(text);print('BOOT',text,flush=True)
            except (OSError,serial.SerialException):stop.wait(.3)
    thread=threading.Thread(target=console,daemon=True);thread.start()
    c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True))
        if args.restart:
            print('Restart response',c.request('/api/device/restart','POST')[0],flush=True)
            c.wait_boot();c.login(progress=lambda p,s:print('RELOGIN',p,s,flush=True))
        for i in range(30):
            state=c.state()
            report['checks'].append({k:state[k] for k in ('cameraReady','startupFailed','freeHeap','freePsram','uptimeSeconds')})
            if i%5==0:print('RUNTIME',report['checks'][-1],flush=True)
            time.sleep(.15)
        settings=c.state('/api/settings/capture')['values']
        code,_,body=c.request('/api/settings/capture','POST',{'values':{'cpuMHz':settings['cpuMHz']}},timeout=30)
        print('Identical camera settings',code,body.decode(),flush=True)
        report['settings_code']=code
        if code!=200 or not state['cameraReady']:raise RuntimeError('Camera settings unavailable in candidate')
        deadline=time.monotonic()+90
        while time.monotonic()<deadline:
            time.sleep(1);state=c.state()
            if not state['cameraReady']:raise RuntimeError('Camera failed after settings/idle transition')
        report['idle_transition_survived']=state['captureIdle']
        from full_mode_tools import inventory
        report['inventory_count']=len(inventory(c));print('Inventory',report['inventory_count'],flush=True)
        state=c.state()
        if not state['cameraReady']:raise RuntimeError('Camera failed after media enumeration')
        report['passed']=True
    finally:
        time.sleep(2);stop.set();thread.join(3);c.clear()
        (ROOT/'.tools/hardware/runtime-diagnostic.json').write_text(json.dumps(report,indent=2),encoding='utf8')

if __name__=='__main__':main()
