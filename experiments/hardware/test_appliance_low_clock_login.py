"""Verify authentication at the lowest offered CPU clock, then restore it.

Keeps the original independent session in RAM for restoration if the slow login
times out. No password/configuration or existing media are changed.
"""
import argparse
import json
from pathlib import Path
import time
from appliance_client import Client

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/low-clock-login')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    owner=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    slow=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],credentials_or_tokens_saved=False)
    original=None
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('LOW-CLOCK','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Low clock assertion: '+name)
    try:
        owner.login();check(not owner.state()['recording'] and not owner.state()['recordRequested'],'no_active_recording')
        original=owner.state('/api/settings/capture')['values']
        check(owner.request('/api/settings/capture','POST',{'values':{'cpuMHz':80}},timeout=30)[0]==200,'lowest_clock_saved')
        check(owner.state()['cpuMHz']==80,'actual_lowest_clock')
        # The product has a 300-second ticket lifetime after measuring 168s
        # at 80 MHz; leave margin below that bound for transport/restoration.
        at=time.monotonic();slow.login(result_timeout=240);report['login_seconds']=time.monotonic()-at
        check(True,'chosen_account_login_at_80mhz');check(slow.state()['cpuMHz']==80,'authenticated_status_at_80mhz')
        check(slow.state('/api/wireguard')['peerSessionUp'],'wireguard_at_80mhz_after_login')
        check(slow.request('/api/camera/still',timeout=30)[0]==200,'camera_at_80mhz_after_login')
        report['passed']=True
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error);print(report['error'],flush=True)
    finally:
        try:
            if original is not None:
                code,_,_=owner.request('/api/settings/capture','POST',{'values':original},timeout=30)
                check(code==200 and owner.state()['cpuMHz']==original['cpuMHz'],'original_capture_values_and_clock_restored')
            if slow.cookie:slow.request('/api/auth/logout','POST')
            if owner.cookie:owner.request('/api/auth/logout','POST')
        except Exception as error:
            report.update(passed=False,restore_error=type(error).__name__)
        owner.clear();slow.clear();report['check_count']=len(report['checks']);(args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
