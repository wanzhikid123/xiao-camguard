"""On-board codec spike. Preserve existing media and restore known-good firmware."""
import json
import argparse
from pathlib import Path
import subprocess
import time
import threading
import serial
from serial_markers import open_console
from urllib.parse import quote
from appliance_client import Client

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'.tools/hardware/codec-probe'

def files(client):
    result=set()
    dates=[];cursor=''
    while True:
        page=client.state('/api/media/dates'+('?before='+cursor if cursor else ''));dates.extend(page['items'])
        if not page['more']:break
        cursor=page['nextBefore']
    for item in dates:
        cursor=''
        while True:
            page=client.state('/api/media?date='+item['date']+('&before='+quote(cursor) if cursor else ''))
            result.update(f['path'] for f in page['items'])
            if not page['more']:break
            cursor=page['nextBefore']
    return result

def wait(client,predicate,seconds=90):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        state=client.state()
        if predicate(state):return state
        time.sleep(.5)
    raise RuntimeError('Capture transition timeout')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--reuse',action='store_true')
    parser.add_argument('--keep-fixture',action='store_true');parser.add_argument('--width',type=int,default=640)
    parser.add_argument('--fps',type=int,default=10);parser.add_argument('--seconds',type=int,default=5)
    parser.add_argument('--check-utc-time',action='store_true');parser.add_argument('--require-wg',action='store_true');args=parser.parse_args()
    global OUT
    OUT=OUT/(f'{args.width}x{240 if args.width==320 else 480}-{args.fps}'+(f'-{args.seconds}s' if args.seconds!=5 else ''))
    OUT.mkdir(parents=True,exist_ok=True)
    client=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
    report={'markers':[]};owned=[];capture=None
    stop=threading.Event()
    def console():
        from diagnose_appliance_runtime import MARKERS
        while not stop.is_set():
            try:
                with open_console() as port:
                    while not stop.is_set():
                        raw=port.readline()
                        if any(marker in raw for marker in MARKERS):
                            line=raw.decode(errors='replace').strip();report['markers'].append(line);print('DIAG',line,flush=True)
            except (OSError,serial.SerialException):stop.wait(.3)
    thread=threading.Thread(target=console,daemon=True);thread.start()
    try:
        print('Logging in to current firmware',flush=True)
        client.login(progress=lambda phase,seconds:print(phase,seconds,flush=True))
        if not client.state()['cameraReady']:
            print('Controlled recovery restart',flush=True)
            client.request('/api/device/restart','POST');client.wait_boot();client.login()
        state=client.state();print('Pre-inventory camera',state['cameraReady'],'startup failed',state['startupFailed'],flush=True)
        before=files(client)
        report['beforeCount']=len(before)
        state=client.state();print('Post-inventory camera',state['cameraReady'],'startup failed',state['startupFailed'],flush=True)
        if not args.reuse:
            print('Installing codec fixture',flush=True)
            report['firmware']=client.install(ROOT/'.pio/build/xiao-appliance-codec-test/firmware.bin')
        if client.request('/api/codecs/test')[0]!=200:raise RuntimeError('Installed fixture did not survive boot health')
        settings=client.state('/api/settings/capture');capture=settings['values']
        report['captureBefore']=capture
        resolution=next(i['value'] for i in settings['videoResolutions'] if i['width']==args.width and i['height']==(240 if args.width==320 else 480))
        code,_,body=client.request('/api/settings/capture','POST',{'values':{'videoResolution':resolution,'fps':args.fps,'cpuMHz':240,'powerSave':0}})
        if code!=200:raise RuntimeError('Test VGA settings rejected: '+str(code)+' '+body.decode())
        print('Recording',args.seconds,'seconds with original settings',flush=True)
        if client.request('/api/record/start','POST')[0]!=202:raise RuntimeError('Record start rejected')
        wait(client,lambda s:s['recording']);time.sleep(args.seconds)
        client.request('/api/record/stop','POST');wait(client,lambda s:not s['recording'] and not s['recordRequested'])
        time.sleep(2)
        owned=sorted(files(client)-before)
        report['ownedTestPaths']=owned
        source=next(p for p in owned if p.endswith('.avi'))
        client.download('/api/media/download?path='+quote(source,safe=''),OUT/'source.avi')
        dimensions=subprocess.run(['ffprobe','-v','error','-select_streams','v','-show_entries','frame=width,height','-of','json',str(OUT/'source.avi')],capture_output=True,text=True,check=True)
        sizes={(f['width'],f['height']) for f in json.loads(dimensions.stdout)['frames']}
        report['actualJpegDimensions']=[list(size) for size in sorted(sizes)]
        assert sizes=={(args.width,240 if args.width==320 else 480)},'AVI contains stale sensor dimensions'
        previous_age=None
        if args.require_wg:
            from full_mode_tools import expected_exit
            expected=expected_exit(client);deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                w=client.state('/api/wireguard')
                if w['fullTunnelVerified'] and w['publicExit'] in expected:break
                time.sleep(1)
            else:raise RuntimeError('Full tunnel not healthy before codec overlap gate')
            previous_age=w['verificationAgeSeconds']
        print('Converting',source,flush=True)
        code,_,body=client.request('/api/codecs/test','POST',{'path':source})
        if code!=202:raise RuntimeError('Codec submission rejected: '+str(code)+' '+body.decode())
        until=time.monotonic()+1200;previous=0;unavailable=0
        while time.monotonic()<until:
            try:result=client.state('/api/codecs/test')
            except (OSError,TimeoutError):
                unavailable+=1;report['httpTimeouts']=unavailable
                print('Codec HTTP probe timed out',unavailable,flush=True)
                if unavailable>=3:raise
                time.sleep(1);continue
            if not result['running']:break
            state=client.state()
            if args.require_wg:
                w=client.state('/api/wireguard')
                if w['routeProbeBusy']:report['tlsBusyObservedDuringConversion']=True
                if w['fullTunnelVerified'] and previous_age is not None and w['verificationAgeSeconds']<previous_age:
                    report['proofRefreshedDuringConversion']=True
                previous_age=w['verificationAgeSeconds']
            report.setdefault('runtime',[]).append({k:state[k] for k in ('freeHeap','freePsram','cpuMHz','cpu0Percent','cpu1Percent','temperatureC') if k in state})
            if time.monotonic()-previous>=20:
                print('Frames',result['frames'],'/',result['totalFrames'],'elapsed',result['elapsedMs'],flush=True);previous=time.monotonic()
            time.sleep(1)
        else:raise RuntimeError('Codec did not finish')
        report['codec']=result
        print('Codec result',json.dumps(result),flush=True)
        if result['outputBytes']:
            client.download('/api/codecs/file',OUT/'output.mp4')
            for name in ('source.avi','output.mp4'):
                probe=subprocess.run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(OUT/name)],capture_output=True,text=True,check=True)
                report[name]=json.loads(probe.stdout)
            decoded=subprocess.run(['ffmpeg','-v','error','-i',str(OUT/'output.mp4'),'-f','null','-'],capture_output=True,text=True)
            report['fullDecode']={'exitCode':decoded.returncode,'errors':decoded.stderr}
            print('Full MP4 decode',decoded.returncode,decoded.stderr,flush=True)
            original=next(s for s in report['source.avi']['streams'] if s['codec_type']=='video')
            video=next(s for s in report['output.mp4']['streams'] if s['codec_type']=='video')
            audio=next(s for s in report['output.mp4']['streams'] if s['codec_type']=='audio')
            assert decoded.returncode==0 and not decoded.stderr
            assert (video['width'],video['height'])==(original['width'],original['height'])
            assert video['codec_name']=='h264' and video['profile'] in ('Baseline','Constrained Baseline')
            assert audio['codec_name']=='aac' and audio['sample_rate']=='16000' and audio['channels']==1
            assert video['nb_frames']==original['nb_frames']
            from fractions import Fraction
            assert Fraction(video['avg_frame_rate'])==Fraction(original['avg_frame_rate'])
            assert abs(float(video['duration'])-float(original['duration']))<=1/args.fps+.005
            assert abs(float(video['duration'])-float(audio['duration']))<=.15
            if args.check_utc_time:
                from datetime import datetime,timezone
                entries=[report['output.mp4']['format'],*report['output.mp4']['streams']]
                tags=[entry.get('tags',{}).get('creation_time') for entry in entries]
                assert all(tags),'UTC creation time missing'
                assert all(abs((datetime.now(timezone.utc)-datetime.fromisoformat(tag.replace('Z','+00:00'))).total_seconds())<180 for tag in tags),'MP4 UTC epoch/timezone mismatch'
                report['utcCreationTimeVerified']=True
            similarity=subprocess.run(['ffmpeg','-i',str(OUT/'source.avi'),'-i',str(OUT/'output.mp4'),'-lavfi',
                '[0:v]settb=AVTB,setpts=N*100000[a];[1:v]settb=AVTB,setpts=N*100000[b];[a][b]ssim','-an','-f','null','-'],capture_output=True,text=True,check=True)
            import re
            match=re.search(r'SSIM Y:([.\d]+).*?U:([.\d]+).*?V:([.\d]+).*?All:([.\d]+)',similarity.stderr,re.S)
            if not match:raise RuntimeError('Image similarity output missing')
            report['ssim']={key:float(value) for key,value in zip(('Y','U','V','All'),match.groups())}
            assert report['ssim']['All']>.8
            if args.require_wg:
                assert report.get('proofRefreshedDuringConversion'),'No successful TLS refresh observed while encoding'
                w=client.state('/api/wireguard');assert w['fullTunnelVerified'] and w['publicExit'] in expected
            report['passed']=True
        if result['error']:raise RuntimeError('Board codec result: '+result['error'])
    finally:
        try:
            if client.request('/api/codecs/test')[0]==401:client.login()
        except Exception:pass
        try:client.request('/api/codecs/file','POST')
        except Exception:pass
        for path in owned:
            try:client.request('/api/media/delete','POST',{'path':path})
            except Exception:pass
        if capture:
            try:client.request('/api/settings/capture','POST',{'values':capture})
            except Exception:pass
        try:
            report['originalPathsRetained']=files(client)==before
            assert report['originalPathsRetained']
        except Exception:report['originalInventoryCheckFailed']=True;report['passed']=False
        print('Keeping fixture for further checks' if args.keep_fixture else 'Restoring known-good production firmware',flush=True)
        try:
            if not args.keep_fixture:client.install(ROOT/'.tools/hardware/appliance-events-final.bin')
        finally:
            stop.set();thread.join(3)
            try:
                s=client.state();report['afterConsoleClose']={k:s[k] for k in ('cameraReady','startupFailed','uptimeSeconds')}
            except Exception:pass
            client.clear()
            (OUT/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
            if report.get('originalInventoryCheckFailed'):raise RuntimeError('Original media inventory changed')

if __name__=='__main__':main()
