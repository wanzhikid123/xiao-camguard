"""Create a new owned MP4 for a browser retry without rerunning queue faults."""
import json,time
from pathlib import Path
from appliance_client import Client
from full_mode_tools import inventory
ROOT=Path(__file__).resolve().parents[2]
def main():
    c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt');before=None;settings=None;owned=set();keep=None
    def wait(predicate,seconds=150):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            s=c.state()
            if predicate(s):return s
            time.sleep(.3)
        raise RuntimeError('Owned browser fixture deadline')
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True))
        if c.request('/api/codecs/test')[0]!=200:raise RuntimeError('Isolated codec fixture required')
        before=inventory(c);data=c.state('/api/settings/capture');settings=data['values']
        resolution=next(x['value'] for x in data['videoResolutions'] if (x['width'],x['height'])==(320,240))
        if c.request('/api/settings/capture','POST',{'values':{'videoResolution':resolution,'fps':5,'cpuMHz':240,'powerSave':0}},timeout=30)[0]!=200:raise RuntimeError('Owned fixture settings refused')
        if c.request('/api/record/start','POST')[0]!=202:raise RuntimeError('Owned recording refused')
        wait(lambda s:s['recording']);time.sleep(5);c.request('/api/record/stop','POST');wait(lambda s:not s['recording'] and not s['recordRequested']);time.sleep(1)
        owned.update(set(inventory(c))-set(before));source=next(x for x in owned if x.endswith('.avi'));output=source[:-4]+'.mp4';owned.add(output)
        if c.request('/api/codecs/test','POST',{'path':source,'mode':'queue_large'})[0]!=202:raise RuntimeError('Owned conversion refused')
        wait(lambda s:s['conversion']['state']=='completed')
        media=inventory(c)
        if source in media or output not in media:raise RuntimeError('Owned MP4 publication incomplete')
        keep=output;(ROOT/'.tools/hardware/conversion-queue/browser-owned.json').write_text(json.dumps({'testOwned':True,'path':output}),encoding='utf8')
        print('OWNED_MP4_READY',flush=True)
    finally:
        c.request('/api/settings/transcode','POST',{'enabled':False});c.request('/api/record/stop','POST')
        for path in owned:
            if path!=keep:c.request('/api/media/delete','POST',{'path':path})
        if settings:c.request('/api/settings/capture','POST',{'values':settings},timeout=30)
        c.clear()
if __name__=='__main__':main()
