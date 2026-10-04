"""Real product settings/telemetry/photo/rename/clock/standby regression gate.

Uses the selected private account. Only newly created media are renamed. No
existing files are removed. Settings are restored in finally; secrets stay RAM.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time
from urllib.parse import quote
from appliance_client import Client
from baseline_http import play_test_tones

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from validate_avi import validate


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--firmware',type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/appliance-refinements')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],credentials_or_tokens_saved=False,photos=[],recordings=[],standby=[])
    original=None
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)));print('REFINE','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Refinement assertion: '+name)
    def save(): (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    def update(values):
        code,_,body=client.request('/api/settings/capture','POST',{'values':values},timeout=30)
        if code!=200: report['settings_failure']=dict(status=code,error=json.loads(body).get('error'))
        check(code==200 and json.loads(body).get('saved'),'settings_saved_'+str(len(report['checks'])))
    def files():
        folder=time.strftime('%Y%m%d',time.gmtime());cursor='';items={}
        for _ in range(32):
            page=client.state('/api/media?date='+folder+('&before='+quote(cursor,safe='') if cursor else ''))
            items.update({item['path']:item for item in page['items']})
            if not page['more']:return items
            cursor=page['nextBefore']
        raise RuntimeError('Media enumeration bound')
    def metadata(file):
        result=subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error','-show_streams','-show_format','-of','json',str(file)],capture_output=True,text=True,check=True,timeout=45)
        return json.loads(result.stdout)
    def decode(file):
        return subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(file),'-f','null','-'],capture_output=True,timeout=60).returncode==0
    def clip(label,tone=False):
        before=files();check(client.request('/api/record/start','POST')[0]==202,'record_start_'+label)
        deadline=time.monotonic()+8
        while not client.state()['recording'] and time.monotonic()<deadline:time.sleep(.15)
        check(client.state()['recording'],'actual_recording_'+label)
        if tone:
            time.sleep(1);play_test_tones();time.sleep(2)
        else:time.sleep(8)
        check(client.request('/api/settings/capture','POST',{'values':{'contrast':1}})[0]==409,'settings_guard_during_record_'+label)
        check(client.request('/api/record/stop','POST')[0]==202,'record_stop_'+label)
        deadline=time.monotonic()+30
        while client.state()['recording'] and time.monotonic()<deadline:time.sleep(.2)
        check(not client.state()['recording'],'record_finalized_'+label)
        added=[path for path in files() if path not in before and path.endswith('.avi')]
        check(len(added)==1,'one_owned_clip_'+label);path=added[0];file=args.out/(label+'.avi')
        client.download('/api/media/download?path='+quote(path,safe=''),file)
        media=validate(metadata(file));check(media['passed'] and decode(file),'complete_mjpeg_pcm_decode_'+label)
        report['recordings'].append(dict(label=label,path=path,bytes=file.stat().st_size,**media))
        return file,path
    try:
        client.login();check(True,'chosen_account_login')
        if args.firmware: report['firmware']=client.install(args.firmware);check(True,'authenticated_ota_over_wireguard')
        state=client.state();check(state['cameraReady'] and state['sdReady'],'camera_sd_after_boot')
        settings=client.state('/api/settings/capture');original=settings['values'];report['defaults']=settings['defaults']
        check(settings['persistent'] and settings['sensor']=='OV3660','persistent_settings_and_real_sensor')
        check(len(original)>=30 and any(item['width']==2048 and item['height']==1536 for item in settings['photoResolutions']),'parameters_and_3mp_capability')
        check(client.request('/api/settings/capture',authorized=False)[0]==401,'anonymous_settings_denied')
        check(client.request('/api/settings/capture','POST',{'values':{'contrast':1}},headers={'X-CSRF-Token':''})[0]==403,'settings_csrf_required')
        for name,value in [('contrast',3),('saturation',-3),('fps',0),('cpuMHz',300),('micGain',8),('idleSeconds',1),('unknown',1),('videoResolution',99)]:
            check(client.request('/api/settings/capture','POST',{'values':{name:value}})[0]==400,'invalid_'+name)
        for label,raw in [('duplicate',b'{"values":{"contrast":0,"contrast":1}}'),('nested',b'{"values":{"contrast":{"x":1}}}'),('array',b'{"values":{"contrast":[0]}}'),('nul',b'{"values":{"contrast":0}}\x00'),('empty',b'{"values":{}}')]:
            check(client.request('/api/settings/capture','POST',raw=raw,headers={'Content-Type':'application/json'})[0]==400,'reject_'+label)
        check(client.state('/api/settings/capture')['values']==original,'invalid_requests_are_atomic')
        update({'powerSave':0})
        baseline_files=files()
        resolutions=settings['photoResolutions']
        for item in resolutions:
            update({'photoResolution':item['value']})
            code,_,body=client.request('/api/camera/photo','POST',timeout=30);check(code==201,'photo_saved_'+str(item['width']))
            saved=json.loads(body);file=args.out/(str(item['width'])+'x'+str(item['height'])+'.jpg')
            check(client.download('/api/media/download?path='+quote(saved['path'],safe=''),file)==saved['bytes'],'photo_complete_download_'+str(item['width']))
            stream=metadata(file)['streams'][0]
            check(stream['width']==item['width'] and stream['height']==item['height'] and decode(file),'photo_actual_dimensions_'+str(item['width']))
            report['photos'].append(dict(path=saved['path'],width=stream['width'],height=stream['height'],bytes=saved['bytes']))
        owned=report['photos'][-1]['path'];before_hash=hashlib.sha256(file.read_bytes()).hexdigest()
        for label in ('../escape','bad.jpg','bad/name','bad\\name',' bad','bad ','x'*65):
            check(client.request('/api/media/rename','POST',{'path':owned,'name':label})[0]==400,'rename_invalid_'+str(len(report['checks'])))
        raw=json.dumps({'path':owned,'name':'庭院 Grüße 01'},ensure_ascii=False).encode('utf8')
        code,_,body=client.request('/api/media/rename','POST',raw=raw,headers={'Content-Type':'application/json'});check(code==200,'utf8_photo_rename')
        renamed=json.loads(body)['path'];client.download('/api/media/download?path='+quote(renamed,safe=''),args.out/'renamed-photo.jpg')
        check(hashlib.sha256((args.out/'renamed-photo.jpg').read_bytes()).hexdigest()==before_hash,'rename_preserves_photo_bytes')
        check(renamed in files() and owned not in files(),'renamed_photo_enumeration')
        update({'photoResolution':original['videoResolution']})
        for mhz in (80,160,240):
            update({'cpuMHz':mhz});check(client.state()['cpuMHz']==mhz,'actual_cpu_clock_'+str(mhz))
            file,path=clip('clock-'+str(mhz));check(client.state('/api/wireguard')['peerSessionUp'],'wg_after_clock_'+str(mhz))
            if mhz==240:
                code,_,body=client.request('/api/media/rename','POST',{'path':path,'name':'renamed-video'})
                check(code==200,'owned_video_rename');new_path=json.loads(body)['path']
                client.download('/api/media/download?path='+quote(new_path,safe=''),args.out/'renamed-video.avi')
                check(hashlib.sha256(file.read_bytes()).digest()==hashlib.sha256((args.out/'renamed-video.avi').read_bytes()).digest(),'rename_preserves_video_bytes')
        for gain in (3,5):
            update({'micGain':gain});file,path=clip('gain-'+str(gain),tone=True)
            pcm=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-i',str(file),'-map','0:a:0','-f','s16le','-'],capture_output=True,check=True,timeout=30).stdout
            samples=array.array('h',pcm);window=4000;peaks=[]
            for start in range(0,len(samples)-window+1,window):
                segment=samples[start:start+window];re=sum(v*math.cos(2*math.pi*880*i/16000) for i,v in enumerate(segment));im=sum(v*math.sin(2*math.pi*880*i/16000) for i,v in enumerate(segment));peaks.append(2*math.hypot(re,im)/window)
            report.setdefault('audio',{})[str(gain)]=dict(tone_amplitude=sum(sorted(peaks,reverse=True)[:4])/4,clipping_fraction=sum(abs(v)>=32767 for v in samples)/max(1,len(samples)))
        low,high=report['audio']['3'],report['audio']['5'];report['audio']['ratio']=high['tone_amplitude']/max(.001,low['tone_amplitude'])
        check(low['tone_amplitude']>1 and 2<report['audio']['ratio']<6,'physical_tone_is_louder_at_4x_gain')
        check(high['clipping_fraction']<.01,'controlled_tone_not_materially_clipped')
        update({'powerSave':1,'idleSeconds':30,'micGain':5,'cpuMHz':240,'photoResolution':original['photoResolution']})
        for cycle in range(3):
            deadline=time.monotonic()+45
            while not client.state()['captureIdle'] and time.monotonic()<deadline:time.sleep(1)
            sleeping=client.state();check(sleeping['captureIdle'] and sleeping['cameraReady'],'actual_camera_standby_'+str(cycle))
            check(client.state('/api/wireguard')['peerSessionUp'],'wg_reachable_in_standby_'+str(cycle))
            started=time.monotonic();code,_,body=client.request('/api/camera/still',timeout=30)
            check(code==200 and body[:2]==b'\xff\xd8','still_wakes_camera_'+str(cycle));report['standby'].append(dict(wake_s=time.monotonic()-started,freeHeap=sleeping['freeHeap'],freePsram=sleeping['freePsram']))
        time.sleep(2.2);state=client.state();check(state['cpuUsageAvailable'] and all(0<=state[k]<=100 for k in ('cpu0Percent','cpu1Percent')),'real_cpu_stats_in_range')
        check(state['totalHeap']>state['freeHeap']>65536 and state['totalPsram']>=state['freePsram']>2000000,'separate_heap_psram_metrics')
        report['telemetry']={k:state[k] for k in ('cpu0Percent','cpu1Percent','totalHeap','freeHeap','totalPsram','freePsram')}
        update(original)
        check(client.request('/api/device/restart','POST')[0]==202,'normal_restart_after_settings')
        client.wait_boot();client.login();check(client.state('/api/settings/capture')['values']==original,'settings_persist_after_reset')
        after=files();check(all(path in after and after[path]['bytes']==item['bytes'] for path,item in baseline_files.items()),'all_preexisting_media_preserved')
        check(client.state()['cameraReady'] and client.state('/api/wireguard')['peerSessionUp'],'camera_and_wg_after_reset')
        report['passed']=True
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error);print(report['error'],flush=True)
    finally:
        if original is not None:
            try:
                client.request('/api/record/stop','POST');time.sleep(2)
                client.request('/api/settings/capture','POST',{'values':original},timeout=30)
            except Exception: report['restore_error']='Settings restoration requires follow-up'
        client.clear();report['check_count']=len(report['checks']);save()
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
