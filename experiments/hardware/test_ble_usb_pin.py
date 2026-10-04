"""Compare explicit USB PIN recovery with authenticated Web, without logging PIN."""
import json,re,time
from pathlib import Path
from appliance_client import Client
from serial_markers import open_console
ROOT=Path(__file__).resolve().parents[2]
def main():
    c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt');report={'passed':False,'checks':[]}
    def check(ok,name):
        report['checks'].append({'name':name,'passed':bool(ok)});print('USB',bool(ok),name,flush=True)
        if not ok:raise RuntimeError(name)
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True))
        before=c.state('/api/settings/ble')['pin']
        with open_console() as port:
            def command(text):
                port.reset_input_buffer();port.write((text+'\n').encode());until=time.monotonic()+15
                while time.monotonic()<until:
                    match=re.fullmatch(rb'BLE PIN: (\d{6})',port.readline().strip())
                    if match:return match[1].decode()
                raise RuntimeError('Explicit USB PIN reply deadline')
            pin=command('BLE PIN');check(pin==before,'usb_query_matches_authenticated_web')
            pin=command('BLE PIN ROTATE');time.sleep(.5)
            check(pin==c.state('/api/settings/ble')['pin'] and pin!=before,'usb_rotation_saved_and_recovery_pin_matches')
            pin=before=''
        check(c.state()['cameraReady'],'usb_console_close_does_not_reset_camera');report['passed']=True
    finally:
        c.clear();(ROOT/'.tools/hardware/ble-usb-report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if __name__=='__main__':main()
