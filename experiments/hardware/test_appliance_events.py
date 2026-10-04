"""Real VGA/PDM/SD event integration with RAM-only detector scene substitutions.

Requires the explicit event fixture and restores the supplied normal image.
Never removes original media. Does not claim a physical-motion detection test.
"""
import argparse
import json
from pathlib import Path
import time
from urllib.parse import quote
from appliance_client import Client
from full_mode_tools import ROOT, inventory, decode_avi, decode_jpeg, expected_exit


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--firmware',type=Path,required=True)
    parser.add_argument('--restore',type=Path,required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/events-fixture')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    c=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],physical_motion_tested=False,credentials_or_tokens_saved=False,clips=[])
    old=None;installed=False;owned=[];original_values=None
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)));print('EVENT','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Event check: '+name)
    def wait(predicate,seconds=12):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            state=c.state()
            if predicate(state):return state
            time.sleep(.15)
        report['last_wait_state']=state;raise RuntimeError('Event wait deadline')
    def scene(name):check(c.request('/api/events/test','POST',{'scene':name})[0]==200,'fixture_scene_'+name)
    def wait_full(seconds=90):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            wg=c.state('/api/wireguard')
            if wg['fullTunnelVerified']:return wg
            time.sleep(1)
        report['wireguard_wait_failure']=wg;return wg
    def watch(seconds,no_record=False):
        deadline=time.monotonic()+seconds;samples=[]
        while time.monotonic()<deadline:
            state=c.state();samples.append({k:state[k] for k in ('recording','captureIdle','freeHeap','freePsram','cpu0Percent','cpu1Percent','events')})
            if no_record and (state['recording'] or state['events']['error']!='none'):raise RuntimeError('Unexpected static event or decode error')
            time.sleep(.3)
        return samples
    try:
        c.login(progress=lambda phase,seconds:print('LOGIN',phase,seconds,flush=True));expected=expected_exit(c)
        old=inventory(c);report['original_media']=old;original_values=c.state('/api/settings/capture')['values']
        report['installation']=c.install(args.firmware);installed=True
        check(c.request('/api/settings/capture','POST',{'values':{'idleSeconds':30}},timeout=30)[0]==200,'temporary_thirty_second_idle_policy')
        values=c.state('/api/settings/capture')['values']
        check(c.request('/api/events/test','POST',{'scene':'static'},authorized=False)[0]==403,'fixture_auth_required')
        scene('static')
        code,_,body=c.request('/api/events/test','POST',{'scene':'selftest'})
        check(code==200 and json.loads(body)=={'passed':6,'total':6},'six_detector_algorithm_cases_on_device')
        for body in ({'sensitivity':0},{'sensitivity':11},{'sensitivity':1.5},{'other':5}):
            check(c.request('/api/events/start','POST',body)[0]==400,'invalid_sensitivity_'+str(len(report['checks'])))
        check(c.request('/api/events/start','POST',{'sensitivity':5},headers={'X-CSRF-Token':''})[0]==403,'start_csrf_required')
        check(c.request('/api/events/start?x=1','POST',{'sensitivity':5})[0]==400,'start_query_denied')
        check(c.request('/api/events/start','POST',{'sensitivity':5})[0]==202,'arm_requested')
        state=wait(lambda s:s['events']['armed'])
        check(state['videoWidth']==640 and state['videoHeight']==480 and state['fps']==15,'runtime_fixed_vga_15_target')
        check(c.state('/api/settings/capture')['values']==values,'arming_does_not_persist_video_override')
        check(c.request('/api/record/start','POST')[0]==409,'manual_start_blocked_while_armed')
        check(c.request('/api/camera/photo','POST')[0]==409,'photo_resolution_change_blocked_while_armed')
        check(c.request('/api/settings/capture','POST',{'values':{'brightness':0}})[0]==409,'sensor_changes_blocked_while_armed')
        check(c.request('/api/device/restart','POST')[0]==409,'restart_blocked_while_armed')
        for name in ('static','noise','lighting'):
            scene(name);report[name]=watch(35 if name=='static' else 5,no_record=True)
            check(c.state()['events']['savedClips']==0,'no_saved_clip_'+name)
        check(not c.state()['captureIdle'] and not c.state()['cameraSensorSleeping'],'armed_mode_overrides_thirty_second_idle_sleep')
        scene('static');check(c.request('/api/auth/logout','POST')[0]==200,'monitoring_session_logged_out')
        c.login(progress=lambda phase,seconds:print('RELOGIN',phase,seconds,flush=True))
        check(c.state()['events']['armed'] and not c.state()['captureIdle'],'monitoring_continues_without_logged_in_session')
        scene('motion');wait(lambda s:s['recording']);watch(2)
        check(c.state()['recording'] and c.state()['events']['savedClips']==0,'opening_does_not_underflow_clip_duration')
        scene('static');wait(lambda s:s['events']['quietPending']);time.sleep(.35)
        check(c.state()['recording'],'quiet_tail_held_in_ram')
        scene('motion');wait(lambda s:s['recording'] and not s['events']['quietPending']);watch(2)
        scene('static');wait(lambda s:not s['recording'] and s['events']['savedClips']==1)
        check(True,'resumed_motion_joins_one_clip_and_static_closes_it')
        scene('motion');wait(lambda s:s['recording']);time.sleep(.55)
        scene('static');wait(lambda s:not s['recording'] and s['events']['savedClips']==2)
        check(True,'brief_event_retained_without_five_second_minimum')
        scene('motion');wait(lambda s:s['recording']);watch(1)
        check(c.request('/api/events/stop','POST')[0]==202,'disarm_during_event')
        state=wait(lambda s:not s['events']['armed'] and not s['events']['requested'] and not s['recording'])
        check(state['events']['savedClips']==3,'disarm_finalizes_third_event')
        check(c.state('/api/settings/capture')['values']==values,'all_stored_settings_preserved')
        report['disarmed_video_width']=state['videoWidth']
        check(state['videoWidth']==c.state('/api/settings/capture')['videoResolutions'][[x['value'] for x in c.state('/api/settings/capture')['videoResolutions']].index(values['videoResolution'])]['width'],'sensor_resolution_restored')
        media=inventory(c);owned=[p for p in media if p not in old and '_event_' in p]
        check(len(owned)==3,'three_separate_unique_event_paths')
        for i,path in enumerate(sorted(owned)):
            target=args.out/f'event-{i}.avi';c.download('/api/media/download?path='+quote(path,safe=''),target)
            avi=decode_avi(target);report['clips'].append(dict(path=path,validation=avi))
            video=next(s for s in avi['probe']['streams'] if s['codec_type']=='video')
            check(avi['passed'] and avi['whole_decode_passed'] and video['width']==640 and video['height']==480,'whole_vga_mjpeg_pcm_decode_'+str(i))
            check(avi['duration_difference_s']<.25,'audio_video_sync_under_250ms_'+str(i))
        durations=[item['validation']['durations_s']['video'] for item in report['clips']]
        check(min(durations)<2 and max(durations)>3 and max(durations)<8,'short_event_saved_and_long_quiet_tail_excluded')
        check(all(media.get(path)==size for path,size in old.items()),'all_original_media_preserved')
        code,_,jpg=c.request('/api/camera/still',timeout=30)
        check(code==200 and decode_jpeg(jpg,args.out/'after-events.jpg'),'camera_restored_jpeg_decode')
        wg=c.state('/api/wireguard');check(wg['fullTunnelVerified'] and wg['publicExit'] in expected,'full_tunnel_survives_event_recording')
        check(c.request('/api/settings/capture','POST',{'values':original_values},timeout=30)[0]==200,'restore_original_idle_policy')
        check(c.state('/api/settings/capture')['values']==original_values,'all_original_values_restored')
        report.update(passed=True,original_media_count=len(old))
    except Exception as failure:
        report['error_type']=type(failure).__name__;print('EVENT FAIL',str(failure),flush=True)
    finally:
        if installed:
            try:
                c.request('/api/events/stop','POST');wait(lambda s:not s['events']['armed'] and not s['recording'])
                if c.state('/api/settings/capture')['values']!=original_values:
                    check(c.request('/api/settings/capture','POST',{'values':original_values},timeout=30)[0]==200,'cleanup_restores_original_values')
                report['restore']=c.install(args.restore)
                check(c.request('/api/events/test')[0]==404,'normal_image_has_no_scene_fixture')
                media=inventory(c)
                report['created_event_paths']=[p for p in media if p not in old and '_event_' in p]
                check(old is not None and all(media.get(path)==size for path,size in old.items()),'originals_preserved_after_normal_restore')
                check(not c.state()['events']['armed'] and wait_full()['fullTunnelVerified'],'normal_restore_disarmed_and_full_tunnel_healthy')
            except Exception as failure:
                report['passed']=False;report['restore_error_type']=type(failure).__name__;print('EVENT RESTORE FAIL',type(failure).__name__,flush=True)
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');c.clear()
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
