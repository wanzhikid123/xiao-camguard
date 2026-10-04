"""Real feature acceptance. Mutate/delete only explicitly owned test media.

Authentication material stays in RAM. Historical AVI conversion remains off
in the codec fixture. Reboot and settings persistence checks keep user values.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from urllib.parse import quote
from appliance_client import Client
from full_mode_tools import inventory

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--ble',action='store_true')
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/features')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],private_values_saved=False)
    sessions=set();owned=set();old=None;capture=None;conversion=None
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('FEATURE','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError(name)
    def wait(predicate,seconds=30):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            state=c.state()
            if predicate(state):return state
            time.sleep(.25)
        raise RuntimeError('Feature state deadline')
    def tl_start(interval=5,immediate=True):
        code,_,_=c.request('/api/timelapse/start','POST',{'intervalSeconds':interval,'immediate':immediate})
        check(code==201,'timelapse_start_'+str(len(sessions)))
        state=c.state('/api/timelapse');sessions.add(state['session']);return state['session']
    def stop():
        check(c.request('/api/timelapse/stop','POST')[0]==202,'timelapse_stop_'+str(len(report['checks'])))
        return wait(lambda s:not s['timelapse']['active'])['timelapse']
    def delete(session):
        if session not in sessions:raise RuntimeError('Session deletion ownership guard')
        return c.request('/api/timelapse/delete','POST',{'session':session})[0]
    try:
        c.login(progress=lambda phase,seconds:print('LOGIN',phase,seconds,flush=True))
        check(c.request('/api/codecs/test')[0]==200,'non_destructive_fixture_installed')
        old=inventory(c);capture=c.state('/api/settings/capture')['values']
        conversion=c.state('/api/settings/transcode')['enabled']
        check(not conversion,'historical_avi_background_processing_inhibited')
        check(c.request('/api/settings/transcode','POST',{'enabled':False})[0]==200,'disable_transcode_saved_explicitly')
        for path in ('/api/timelapse','/api/timelapse/sessions','/api/settings/ble','/api/settings/transcode'):
            check(c.request(path,authorized=False)[0]==401,'anonymous_denied_'+path)
        for path,body in (('/api/timelapse/start',{'intervalSeconds':5,'immediate':True}),
                          ('/api/timelapse/delete',{'session':'TL_invalid'}),('/api/settings/transcode',{'enabled':False})):
            check(c.request(path,'POST',body,headers={'X-CSRF-Token':''})[0]==403,'csrf_required_'+path)
            check(c.request(path,'POST',body,headers={'Origin':'http://example.invalid'})[0]==403,'origin_required_'+path)
        for value in (4,604801,5.5,'5'):
            check(c.request('/api/timelapse/start','POST',{'intervalSeconds':value,'immediate':True})[0]==400,'interval_rejected_'+str(value))
        for raw in (b'{"intervalSeconds":5,"intervalSeconds":6,"immediate":true}',b'{"intervalSeconds":5,"immediate":1}',
                    b'{"intervalSeconds":5,"immediate":true,"extra":1}',b'{"intervalSeconds":5,"immediate":true}\x00'):
            check(c.request('/api/timelapse/start','POST',raw=raw,headers={'Content-Type':'application/json'})[0]==400,'malformed_tl_'+str(len(report['checks'])))
        session=tl_start()
        check(c.request('/api/timelapse/delete','POST',{'session':session})[0]==409,'running_session_undeletable')
        check(c.request('/api/events/start','POST',{'sensitivity':2})[0]==409,'events_require_timelapse_stop')
        check(c.request('/api/settings/capture','POST',{'values':{'photoQuality':12}})[0]==409,'capture_settings_locked')
        state=wait(lambda s:s['timelapse']['photos']>=2,25)['timelapse']
        check(state['width']>0 and state['failures']==0,'real_immediate_and_scheduled_jpeg_capture')
        page=c.state('/api/timelapse/files?session='+session)
        path=page['items'][0]['path'];jpeg=args.out/'scheduled.jpg'
        c.download('/api/media/download?path='+quote(path,safe=''),jpeg)
        probe=subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error','-show_streams','-of','json',str(jpeg)],capture_output=True,text=True,check=True)
        stream=json.loads(probe.stdout)['streams'][0]
        check((stream['width'],stream['height'])==(state['width'],state['height']),'scheduled_photo_dimensions_match_snapshot')
        code,headers,body=c.request('/api/media/file?path='+quote(path,safe=''),headers={'Range':'bytes=0-31'})
        check(code==206 and body==jpeg.read_bytes()[:32] and headers.get('Accept-Ranges')=='bytes','photo_range_exact_bytes')
        code,headers,body=c.request('/api/media/file?path='+quote(path,safe=''),headers={'Range':'bytes=-32'})
        check(code==206 and body==jpeg.read_bytes()[-32:],'photo_suffix_range_exact_bytes')
        check(c.request('/api/media/file?path='+quote(path,safe=''),headers={'Range':'bytes=999999999-'})[0]==416,'unsatisfiable_range')
        check(c.request('/api/record/start','POST')[0]==202,'manual_recording_allowed_during_timelapse')
        wait(lambda s:s['recording']);time.sleep(7)
        check(c.request('/api/record/stop','POST')[0]==202,'manual_recording_stop')
        wait(lambda s:not s['recording'] and not s['recordRequested'])
        state=wait(lambda s:s['timelapse']['photos']>state['photos'],15)['timelapse']
        check(state['skipped']>0 and state['failures']==0,'recording_skips_are_not_photo_failures')
        owned.update(set(inventory(c))-set(old))
        final=stop();check(final['photos']>=3,'stop_preserves_completed_photo_count')
        rows=c.state('/api/timelapse/sessions')['items'];item=next(x for x in rows if x['name']==session)
        check(not item['running'] and not item['metadata']['active'] and item['metadata']['photos']==final['photos'],'session_metadata_finalized')
        check(delete(session)==200,'owned_stopped_session_deleted')
        check(c.request('/api/events/start','POST',{'sensitivity':2})[0]==202,'events_start_after_timelapse_stop')
        wait(lambda s:s['events']['armed'])
        check(c.request('/api/timelapse/start','POST',{'intervalSeconds':5,'immediate':True})[0]==409,'timelapse_requires_events_stop')
        check(c.request('/api/events/stop','POST')[0]==202,'event_stop_after_mutual_exclusion')
        wait(lambda s:not s['events']['armed'] and not s['events']['requested'] and not s['recording'])
        owned.update(set(inventory(c))-set(old))
        session=tl_start(5,False)
        check(c.state('/api/timelapse')['photos']==0,'delayed_first_photo')
        wait(lambda s:s['timelapse']['photos']>=21,130)
        stop();page=c.state('/api/timelapse/files?session='+session)
        check(len(page['items'])==20 and page['more'],'photo_page_is_bounded_to_20')
        next_page=c.state('/api/timelapse/files?session='+session+'&before='+page['nextBefore'])
        check(len(next_page['items'])>=1 and not {x['name'] for x in page['items']} & {x['name'] for x in next_page['items']},'photo_cursor_has_no_duplicates')
        check(delete(session)==200,'owned_paginated_session_deleted')
        if args.ble:
            import appliance_ble_checks
            appliance_ble_checks.run(c,check)
        session=tl_start(604800,False)
        check(c.request('/api/device/restart','POST')[0]==202,'restart_stops_timelapse_cleanly')
        c.wait_boot();c.login(progress=lambda phase,seconds:print('RELOGIN',phase,seconds,flush=True))
        check(not c.state('/api/timelapse')['active'],'no_automatic_resume_after_reboot')
        check(c.state('/api/settings/capture')['values']==capture,'user_capture_settings_preserved')
        check(not c.state('/api/settings/transcode')['enabled'],'disabled_transcode_persists')
        check(delete(session)==200,'rebooted_session_is_deletable')
        report['telemetry']={k:c.state()[k] for k in ('freeHeap','minimumFreeHeap','freePsram','cpuMHz')}
        report['passed']=True
    finally:
        try:
            c.request('/api/record/stop','POST');c.request('/api/events/stop','POST');c.request('/api/timelapse/stop','POST')
            time.sleep(2)
            for session in sessions:
                if any(x['name']==session for x in c.state('/api/timelapse/sessions')['items']):delete(session)
            if old is not None:
                owned.update(set(inventory(c))-set(old))
                for path in owned:c.request('/api/media/delete','POST',{'path':path})
                after=inventory(c);check(all(after.get(p)==size for p,size in old.items()),'every_preexisting_media_path_and_size_retained')
            if capture is not None:c.request('/api/settings/capture','POST',{'values':capture},timeout=30)
        finally:
            c.clear();(args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')

if __name__=='__main__':main()
