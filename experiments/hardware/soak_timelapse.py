"""Owned real SD/camera soak. Default: the plan's six hours at one minute."""
import argparse,json,time
from pathlib import Path
from appliance_client import Client
from full_mode_tools import inventory
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--seconds',type=int,default=21600)
    parser.add_argument('--interval',type=int,default=60);args=parser.parse_args()
    c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
    report={'passed':False,'seconds':args.seconds,'interval':args.interval,'samples':[]};session=None;old=None
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True));old=inventory(c)
        if c.request('/api/timelapse/start','POST',{'intervalSeconds':args.interval,'immediate':True})[0]!=201:raise RuntimeError('Soak start rejected')
        session=c.state('/api/timelapse')['session'];started=time.monotonic();last=-60
        while time.monotonic()-started<args.seconds:
            state=c.state();tl=state['timelapse']
            if not tl['active'] or tl['session']!=session or tl['failures']:raise RuntimeError('Owned soak stopped or failed')
            if not state['cameraReady'] or not state['sdReady'] or not state['wifiConnected'] or not state['ble']['ready']:
                raise RuntimeError('Camera/SD/Wi-Fi/BLE health lost during owned soak')
            if time.monotonic()-started-last>=60:
                wg=c.state('/api/wireguard')
                sample={'elapsed':round(time.monotonic()-started),'photos':tl['photos'],'skipped':tl['skipped'],
                        'freeHeap':state['freeHeap'],'freePsram':state['freePsram'],'wifiConnected':state['wifiConnected'],'bleReady':state['ble']['ready'],
                        'fullTunnelVerified':wg['fullTunnelVerified'],'verificationAgeSeconds':wg['verificationAgeSeconds'],'routeProbeHTTP':wg['routeProbeHTTP']}
                report['samples'].append(sample);print('SOAK',json.dumps(sample),flush=True);last=time.monotonic()-started
            time.sleep(1)
        c.request('/api/timelapse/stop','POST');until=time.monotonic()+30
        while c.state('/api/timelapse')['active'] and time.monotonic()<until:time.sleep(.3)
        final=c.state('/api/timelapse');report['final']=final
        photos=set();cursor=''
        while True:
            page=c.state('/api/timelapse/files?session='+session+('&before='+cursor if cursor else ''))
            if page['enumerationTruncated']:raise RuntimeError('Incomplete soak photo enumeration')
            names={x['name'] for x in page['items']}
            if photos&names:raise RuntimeError('Repeated soak photo page')
            photos.update(names)
            if not page['more']:break
            cursor=page['nextBefore']
        report['fileCount']=len(photos)
        expected=(args.seconds+args.interval-1)//args.interval;report['expectedPhotos']=expected
        if not expected<=final['photos']<=expected+1:raise RuntimeError('Soak capture count differs from elapsed schedule')
        if len(photos)!=final['photos'] or final['failures'] or final['skipped'] or final['active']:raise RuntimeError('Soak file/scheduler accounting mismatch')
        after=inventory(c)
        if not all(after.get(p)==n for p,n in old.items()):raise RuntimeError('Historical media changed during soak')
        report['passed']=True
    finally:
        if session:
            c.request('/api/timelapse/stop','POST');time.sleep(2)
            c.request('/api/timelapse/delete','POST',{'session':session})
        c.clear();(ROOT/f'.tools/hardware/timelapse-soak-{args.seconds}s.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if __name__=='__main__':main()
