"""Observe automatic public-proof refreshes on an already confirmed full mode.

Read-only apart from creating/logging out its session; keeps keys and cookies
in RAM, does not restart the peer or change any configuration/media.
"""
import argparse
import json
import time
from pathlib import Path
from appliance_client import Client
from full_mode_tools import ROOT, expected_exit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--seconds', type=int, default=190)
    parser.add_argument('--out', type=Path, default=ROOT/'.tools/hardware/full-refresh-final')
    args = parser.parse_args()
    if not 150 <= args.seconds <= 600:
        parser.error('Use 150..600 seconds to observe multiple automatic refreshes')
    args.out.mkdir(parents=True, exist_ok=True)
    client = Client(args.address, ROOT/'.tools/private/admin-password.txt')
    exits = expected_exit(client)
    report = dict(passed=False, samples=[], refreshes=0, credentials_or_tokens_saved=False)
    try:
        client.login()
        before = client.state()
        revision = before['configRevision']
        if before['recording'] or client.state('/api/network')['revision']:
            raise RuntimeError('Requires idle confirmed configuration')
        started = time.monotonic()
        previous_age = None
        while time.monotonic()-started < args.seconds:
            state = client.state()
            wg = client.state('/api/wireguard')
            if (wg['mode'] != 'full_tunnel' or not wg['guardReady']
                or not wg['fullTunnelVerified'] or wg['publicExit'] not in exits
                or not state['cameraReady'] or not state['sdReady']
                or state['configRevision'] != revision or state['uptimeSeconds'] < before['uptimeSeconds']):
                raise RuntimeError('Verified full mode or device health was lost')
            age = wg['verificationAgeSeconds']
            if previous_age is not None and age < previous_age:
                report['refreshes'] += 1
                print('FULL-REFRESH actual automatic proof', report['refreshes'], flush=True)
            previous_age = age
            report['samples'].append(dict(seconds=round(time.monotonic()-started, 2),
                age=age, busy=wg['routeProbeBusy'], freeHeap=state['freeHeap'],
                http=wg['routeProbeHTTP'], sent=wg['sentBytes'], received=wg['receivedBytes']))
            time.sleep(3)
        idle = [item['freeHeap'] for item in report['samples'] if not item['busy']]
        report['completed_probe_heap_range'] = [min(idle), max(idle)]
        report['completed_probe_heap_drop'] = idle[0]-idle[-1]
        report['elapsed_seconds'] = round(time.monotonic()-started, 2)
        report['passed'] = report['refreshes'] >= 2 and report['completed_probe_heap_drop'] <= 4096
        print('FULL-REFRESH', 'PASS' if report['passed'] else 'FAIL',
              'automatic refresh and TLS memory recovery', flush=True)
    except Exception as error:
        report['error_type'] = type(error).__name__
        print('FULL-REFRESH FAIL', type(error).__name__, flush=True)
    finally:
        try:
            if client.cookie:
                client.request('/api/auth/logout', 'POST')
        except Exception as error:
            report['passed'] = False
            report['cleanup_error_type'] = type(error).__name__
        client.clear()
        (args.out/'report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
