"""Read-only WG transfer of an explicitly named, previously owned large AVI.

Optionally waits for an independent idle gate to pass before creating any
network load. Existing media, configuration and firmware remain untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from urllib.parse import quote
from urllib.request import Request
from appliance_client import Client
from baseline_http import HTTP

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--path',required=True)
    parser.add_argument('--reference',type=Path,required=True)
    parser.add_argument('--after-idle-report',type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/appliance-large-wg')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    if not args.reference.is_file():raise RuntimeError('Previously verified reference file required')
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],samples=[],bytes=0,credentials_or_tokens_saved=False)
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('LARGE-WG','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Large transfer assertion: '+name)
    def save():(args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    try:
        if args.after_idle_report:
            print('LARGE-WG waiting for independent idle gate',flush=True);deadline=time.monotonic()+2400
            while time.monotonic()<deadline:
                try:data=json.loads(args.after_idle_report.read_text(encoding='utf8'))
                except (OSError,ValueError):data={}
                if data.get('passed'):break
                if data.get('error'):raise RuntimeError('Prior idle gate failed; transfer not started')
                time.sleep(5)
            check(bool(data.get('passed')),'independent_idle_gate_passed_before_load')
        client.login();check(True,'chosen_account_login')
        before=client.state();check(before['cameraReady'] and before['sdReady'] and not before['recording'] and not before['recordRequested'],'camera_sd_ready_recording_finalized')
        check(client.state('/api/wireguard')['peerSessionUp'],'authenticated_wireguard_available')
        expected_size=args.reference.stat().st_size;expected_hash=hashlib.sha256(args.reference.read_bytes()).hexdigest()
        actual=hashlib.sha256();started=time.monotonic();probe_at=announced=0
        with HTTP.open(Request(client.base+'/api/media/download?path='+quote(args.path,safe=''),headers={'Cookie':client.cookie}),timeout=10) as stream, (args.out/'download.avi').open('wb') as file:
            check(stream.code==200,'large_download_admitted')
            while chunk:=stream.read(65536):
                file.write(chunk);actual.update(chunk);report['bytes']+=len(chunk)
                elapsed=time.monotonic()-started
                if elapsed-probe_at>=30:
                    probe_at=elapsed;at=time.monotonic();state=client.state()
                    check(state['uptimeSeconds']>=before['uptimeSeconds'],'no_reset_during_large_transfer')
                    report['samples'].append(dict(elapsed_s=round(elapsed,3),status_latency_s=round(time.monotonic()-at,4),free_heap=state['freeHeap'],free_psram=state['freePsram'],capture_idle=state['captureIdle']))
                    save()
                if int(elapsed)//60>announced:
                    announced=int(elapsed)//60;print('LARGE-WG elapsed minutes',announced,'bytes',report['bytes'],flush=True)
        report.update(elapsed_s=time.monotonic()-started,sha256=actual.hexdigest(),reference_sha256=expected_hash)
        check(report['bytes']==expected_size and actual.hexdigest()==expected_hash,'whole_large_file_size_and_sha256_match')
        decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(args.out/'download.avi'),'-f','null','-'],capture_output=True,timeout=120)
        check(decoded.returncode==0,'complete_large_mjpeg_pcm_decode')
        after=client.state();check(after['cameraReady'] and after['sdReady'] and client.state('/api/wireguard')['peerSessionUp'],'camera_card_and_wireguard_after_transfer')
        check(after['configRevision']==before['configRevision'],'selected_network_unchanged')
        report['passed']=True
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error);print(report['error'],flush=True)
    finally:
        if client.cookie:
            try:client.request('/api/auth/logout','POST')
            except Exception:pass
        client.clear();report['check_count']=len(report['checks']);save()
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
