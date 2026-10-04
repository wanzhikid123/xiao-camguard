"""Capture only boot/heap/error markers, never raw console or credentials."""
import argparse
import json
from pathlib import Path
import threading
import time
import serial
from serial_markers import open_console
from appliance_client import Client

ROOT=Path(__file__).resolve().parents[2]
MARKERS=(b'initialization failed',b'Management services',b'OTA boot health',b'Guru Meditation',
         b'Backtrace:',b'assert failed:',b'abort()',b'ESP-ROM',b'ELF file SHA256',b'CORRUPT HEAP',b'Native BLE',b'Failed to register')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--firmware',type=Path,required=True)
    parser.add_argument('--keep-candidate',action='store_true');args=parser.parse_args()
    stop=threading.Event();lines=[]
    def console():
        while not stop.is_set():
            try:
                with open_console() as port:
                    while not stop.is_set():
                        raw=port.readline()
                        if any(marker in raw for marker in MARKERS):
                            text=raw.decode(errors='replace').strip();lines.append(text);print('BOOT',text,flush=True)
            except (serial.SerialException,OSError):stop.wait(.3)
    thread=threading.Thread(target=console,daemon=True);thread.start()
    client=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
    healthy=False
    try:
        client.login(progress=lambda phase,seconds:print(phase,seconds,flush=True))
        print('Installing candidate',flush=True);print(client.install(args.firmware),flush=True)
        route=client.request('/api/codecs/test')[0];print('Codec route',route,flush=True)
        state=client.state();print('Heap',state['freeHeap'],'minimum',state['minimumFreeHeap'],'uptime',state['uptimeSeconds'],'camera',state['cameraReady'],'startupFailure',state['startupFailed'],flush=True)
        time.sleep(25)
        healthy=route==200 and client.request('/api/codecs/test')[0]==200 and state['freeHeap']>=32768 and state['cameraReady'] and not state['startupFailed']
        if not healthy:raise RuntimeError('Candidate did not survive the full boot health window')
    finally:
        print('Keeping healthy candidate' if args.keep_candidate and healthy else 'Restoring production',flush=True)
        try:
            if not (args.keep_candidate and healthy):
                try:client.install(ROOT/'.tools/hardware/appliance-events-final.bin')
                except Exception:
                    client.wait_boot();client.login(progress=lambda phase,seconds:print('restore',phase,seconds,flush=True))
                    client.install(ROOT/'.tools/hardware/appliance-events-final.bin')
        finally:
            client.clear();stop.set();thread.join(3)
            (ROOT/'.tools/hardware/feature-boot-markers.json').write_text(json.dumps(lines,indent=2),encoding='utf-8')

if __name__=='__main__':main()
