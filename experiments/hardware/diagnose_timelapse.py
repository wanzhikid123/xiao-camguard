"""Bounded real timelapse replay; retain public states and allowlisted logs."""
import json,threading,time
from pathlib import Path
from appliance_client import Client
from serial_markers import open_console
ROOT=Path(__file__).resolve().parents[2]
c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
report={'states':[],'markers':[]};stop=threading.Event();owned=None
def console():
    with open_console() as port:
        while not stop.is_set():
            raw=port.readline()
            if any(x in raw for x in (b'[TL_DIAG]',b'Camera',b'camera',b'fb_get',b'Guru',b'Backtrace',b'assert failed',b'CORRUPT')):
                line=raw.decode(errors='replace').strip();report['markers'].append(line);print('MARKER',line,flush=True)
t=threading.Thread(target=console,daemon=True);t.start()
try:
    c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True))
    print('START',c.request('/api/timelapse/start','POST',{'intervalSeconds':5,'immediate':True})[0],flush=True)
    owned=c.state('/api/timelapse')['session']
    for i in range(35):
        state=c.state();settings=c.state('/api/settings/capture')
        item={'elapsed':i,'timelapse':state['timelapse'],'captureBusy':state['captureBusy'],'captureIdle':state['captureIdle'],
              'settingsBusy':settings['busy'],'settingsError':settings['error'],'cameraReady':state['cameraReady']}
        report['states'].append(item);print('STATE',json.dumps(item),flush=True);time.sleep(1)
finally:
    c.request('/api/timelapse/stop','POST');time.sleep(3)
    if owned:c.request('/api/timelapse/delete','POST',{'session':owned})
    stop.set();t.join(3);c.clear()
    (ROOT/'.tools/hardware/timelapse-diagnostic.json').write_text(json.dumps(report,indent=2),encoding='utf8')
