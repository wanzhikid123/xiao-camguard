"""Publication/recovery acceptance restricted to explicitly created clips."""
import argparse,json,time,subprocess
from pathlib import Path
from urllib.parse import quote
from appliance_client import Client
from full_mode_tools import inventory
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'.tools/hardware/conversion-queue'
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--keep-mp4',action='store_true',default=True);args=parser.parse_args();published=None
    OUT.mkdir(parents=True,exist_ok=True);c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
    report={'passed':False,'checks':[]};old=None;capture=None;owned=set()
    def check(ok,name):
        report['checks'].append({'name':name,'passed':bool(ok)});print('QUEUE',bool(ok),name,flush=True)
        if not ok:raise RuntimeError(name)
    def wait(predicate,seconds=120):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            state=c.state()
            if predicate(state):return state
            time.sleep(.5)
        raise RuntimeError('Conversion queue deadline')
    def record():
        before=inventory(c);check(c.request('/api/record/start','POST')[0]==202,'owned_record_start_'+str(len(owned)))
        wait(lambda s:s['recording']);time.sleep(5);c.request('/api/record/stop','POST')
        wait(lambda s:not s['recording'] and not s['recordRequested']);time.sleep(1)
        added=set(inventory(c))-set(before);owned.update(added);source=next(x for x in added if x.endswith('.avi'))
        owned.add(source[:-4]+'.mp4');return source
    def queue(source,mode='queue'):
        if source not in owned:raise RuntimeError('Fixture ownership guard')
        check(c.request('/api/codecs/test','POST',{'path':source,'mode':mode})[0]==202,'queue_'+mode+'_'+str(len(report['checks'])))
    def disable():
        c.request('/api/settings/transcode','POST',{'enabled':False});wait(lambda s:not s['conversion']['running'])
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True));old=inventory(c);settings=c.state('/api/settings/capture');capture=settings['values']
        check(c.request('/api/codecs/test')[0]==200,'fixture_only')
        resolution=next(row['value'] for row in settings['videoResolutions'] if (row['width'],row['height'])==(320,240))
        code,_,body=c.request('/api/settings/capture','POST',{'values':{'videoResolution':resolution,'fps':5,'cpuMHz':240,'powerSave':0}},timeout=30)
        if code!=200:report['settingRejection']={'status':code,'error':json.loads(body).get('error')}
        check(code==200,'owned_qvga_settings')
        # Normal publication also deliberately makes the MP4 larger than AVI.
        source=record();size=inventory(c)[source];queue(source,'queue_large')
        wait(lambda s:s['conversion']['state']=='completed',150);media=inventory(c)
        check(source not in media and media[source[:-4]+'.mp4']>size,'qualified_larger_mp4_published_and_avi_deleted')
        published=source[:-4]+'.mp4'
        target=OUT/'published-larger.mp4';c.download('/api/media/download?path='+quote(source[:-4]+'.mp4',safe=''),target)
        decoded=subprocess.run(['ffmpeg','-v','error','-i',str(target),'-f','null','-'],capture_output=True,text=True)
        check(decoded.returncode==0 and not decoded.stderr,'published_larger_mp4_full_audio_video_decode');disable()
        # Stop at the real transaction boundary, then use the actual reboot path.
        source=record();queue(source,'queue_publish');wait(lambda s:not s['conversion']['enabled'] and not s['conversion']['running'] and s['conversion']['error']=='aborted',150)
        media=inventory(c);check(source in media and source[:-4]+'.mp4' in media,'publish_checkpoint_retains_avi_and_closed_mp4')
        check(c.request('/api/device/restart','POST')[0]==202,'transaction_checkpoint_restart');c.wait_boot();c.login()
        queue(source);wait(lambda s:s['conversion']['state']=='completed',90)
        check(source not in inventory(c),'journal_recovery_revalidates_then_deletes_source');disable()
        # Corrupt the owned published file, leaving its journal/source intact.
        source=record();queue(source,'queue_publish');wait(lambda s:not s['conversion']['enabled'] and not s['conversion']['running'] and s['conversion']['error']=='aborted',150)
        check(c.request('/api/codecs/test','POST',{'path':source,'mode':'corrupt'})[0]==200,'owned_mp4_corruption_injected')
        queue(source);wait(lambda s:s['conversion']['state']=='destination_exists',90)
        check(source in inventory(c),'corrupt_mp4_never_authorizes_avi_deletion');disable()
        # Foreground capture must abort and release all codec/camera resources.
        source=record();queue(source);wait(lambda s:s['conversion']['running'],90)
        started=time.monotonic();code,_,body=c.request('/api/camera/photo','POST',timeout=30)
        check(code==201 and time.monotonic()-started<25,'foreground_photo_preempts_conversion')
        owned.add(json.loads(body)['path']);wait(lambda s:not s['conversion']['running'])
        media=inventory(c);check(source in media and source[:-4]+'.mp4' not in media,'aborted_conversion_retains_source_without_published_mp4')
        queue(source);wait(lambda s:s['conversion']['state']=='completed',150)
        check(source not in inventory(c),'aborted_source_restarts_and_finishes');disable()
        source=record()
        check(c.request('/api/codecs/test','POST',{'path':source,'mode':'part'})[0]==200,'owned_interrupted_part_created')
        c.request('/api/device/restart','POST');c.wait_boot();c.login()
        check(source in inventory(c) and not c.state()['conversion']['enabled'],'boot_cleanup_keeps_source_and_disabled_switch')
        check(c.request('/api/codecs/test','POST',{'path':source,'mode':'part'})[0]==200,'boot_removed_only_reserved_interrupted_part')
        c.request('/api/device/restart','POST');c.wait_boot();c.login()
        after=inventory(c)
        check(all(after.get(path)==size for path,size in old.items()),'all_historical_media_paths_and_sizes_preserved')
        report['passed']=True
    finally:
        try:
            disable();c.request('/api/record/stop','POST')
            for path in owned:
                if not (args.keep_mp4 and report['passed'] and path==published):c.request('/api/media/delete','POST',{'path':path})
            if args.keep_mp4 and report['passed']:
                (OUT/'browser-owned.json').write_text(json.dumps({'testOwned':True,'path':published}),encoding='utf8')
            if capture:c.request('/api/settings/capture','POST',{'values':capture},timeout=30)
            if old is not None:
                after=inventory(c);report['originalsRetained']=all(after.get(p)==n for p,n in old.items())
                check(report['originalsRetained'],'original_inventory_matches_after_owned_cleanup')
        finally:c.clear();(OUT/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if __name__=='__main__':main()
