"""Exercise the real lwIP TCP reset path while removing a WG interface.

Use the explicitly marked hardware fixture for the active bound TCP PCB. The
normal image is installed separately afterwards. Serial capture never resets
the board, credentials stay in RAM, and all capture values are restored.
"""
import argparse
import json
from pathlib import Path
import threading
import time
import serial
from appliance_client import Client

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--expect-panic', action='store_true')
    parser.add_argument('--cycles', type=int, default=3)
    parser.add_argument('--restore-cpu', type=int, choices=(80,160,240))
    parser.add_argument('--restore-values', type=Path, help='Explicit public-only recovery values after a prior failed gate')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=True)
    client = Client(args.address, ROOT/'.tools/private/admin-password.txt')
    report = dict(passed=False, checks=[], cycles=[], expected_panic=args.expect_panic,
                  credentials_or_tokens_saved=False)
    original = None; stop = threading.Event(); data = bytearray()
    device = serial.Serial(port=None, baudrate=115200, timeout=.2)
    device.port=args.port; device.dtr=False; device.rts=False; device.open()

    def capture():
        with (args.out/'serial.log').open('wb') as file:
            while not stop.is_set():
                chunk=device.read(device.in_waiting or 1)
                if chunk:
                    data.extend(chunk); file.write(chunk); file.flush()
    reader=threading.Thread(target=capture, daemon=True); reader.start()

    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('WG-TEARDOWN','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('WG teardown assertion: '+name)
    def wait_peer(seconds=30):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            if client.state('/api/wireguard')['peerSessionUp']:return True
            time.sleep(.4)
        return False
    try:
        client.login(progress=lambda phase,seconds:print('WG-TEARDOWN login',phase,seconds,flush=True))
        original=client.state('/api/settings/capture')['values']
        saved_before=dict(original)
        if args.restore_values:
            original=json.loads(args.restore_values.read_text(encoding='utf8'))
            check(set(original)==set(saved_before),'recovery_values_have_exact_capture_fields')
        if args.restore_cpu is not None:original['cpuMHz']=args.restore_cpu
        check(not client.state()['recording'] and not client.state()['recordRequested'],'no_active_recording')
        print('WG-TEARDOWN authenticated fixture installation',flush=True)
        report['installation']=client.install(args.firmware)
        check(client.state('/api/settings/capture')['values']==saved_before,'ota_preserves_all_capture_values')
        report['pre_test_panic_observed']=b'Guru Meditation Error' in data
        check(client.state()['cameraReady'] and not client.state()['startupFailed'],'healthy_camera_after_authenticated_update')
        check(client.request('/api/settings/capture','POST',{'values':{'cpuMHz':240,'powerSave':0}},timeout=30)[0]==200,
              'safe_test_clock_and_awake_camera')
        check(wait_peer(),'actual_wireguard_peer_ready')
        revision=client.state()['configRevision']
        for cycle in range(args.cycles):
            initial=client.state(); offset=len(data)
            check(client.request('/api/wireguard/restart','POST')[0]==202,'restart_accepted_'+str(cycle+1))
            time.sleep(3)
            until=time.monotonic()+8
            while time.monotonic()<until and b'WG teardown fixture: active TCP PCB prepared' not in data[offset:]:
                time.sleep(.1)
            check(b'WG teardown fixture: active TCP PCB prepared' in data[offset:],'real_active_tcp_pcb_'+str(cycle+1))
            panic=b'Guru Meditation Error' in data[offset:]
            report['cycles'].append(dict(cycle=cycle+1,panic_observed=panic))
            if args.expect_panic:
                check(panic,'original_null_state_panic_reproduced')
                client.wait_boot();client.login()
                check(client.state()['cameraReady'] and wait_peer(),'normal_boot_recovers_after_red_reproduction')
                break
            check(not panic,'no_panic_during_real_tcp_teardown_'+str(cycle+1))
            check(wait_peer(),'new_peer_session_'+str(cycle+1))
            current=client.state()
            check(current['uptimeSeconds']>=initial['uptimeSeconds'] and current['configRevision']==revision,
                  'no_reboot_or_config_change_'+str(cycle+1))
            check(current['cameraReady'] and current['sdReady'] and client.request('/api/camera/still',timeout=30)[0]==200,
                  'camera_sd_jpeg_survive_'+str(cycle+1))
            check(client.request('/api/wireguard/probe','POST',{'address':'10.8.0.1'})[0]==202,
                  'authenticated_data_probe_queued_'+str(cycle+1))
            until=time.monotonic()+8;probe={}
            while time.monotonic()<until:
                probe=client.state('/api/wireguard')
                if not probe['probeBusy'] and probe['probeSent']==3:break
                time.sleep(.3)
            check(probe.get('probeReceived',0)>0,'actual_bidirectional_data_after_teardown_'+str(cycle+1))
        report['passed']=True
    except Exception as error:
        report['error_type']=type(error).__name__;print('WG-TEARDOWN failed',type(error).__name__,flush=True)
    finally:
        try:
            if original is not None:
                check(client.request('/api/settings/capture','POST',{'values':original},timeout=30)[0]==200,
                      'original_values_and_recovery_clock_restored')
                check(client.state()['cpuMHz']==original['cpuMHz'],'actual_restored_clock')
            client.request('/api/auth/logout','POST')
        except Exception as error:report.update(passed=False,restore_error=type(error).__name__)
        client.clear();stop.set();reader.join(timeout=2);device.close()
        report['serial_bytes']=len(data);report['check_count']=len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
