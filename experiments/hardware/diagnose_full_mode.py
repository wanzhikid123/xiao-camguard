"""Sanitized production-mode readiness diagnosis, always cancel the trial."""
import json
from pathlib import Path
import time
from appliance_client import Client

ROOT=Path(__file__).resolve().parents[2]
client=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
report={'hypotheses':['physical callbacks bypass linker wrappers','explicit resolver rejected as local','public HTTPS verification cannot resolve or complete','mode prerequisites rejected before apply','network readiness timeout before peer proof'], 'samples':[]}
try:
    client.login()
    print('INITIAL',json.dumps(client.state('/api/wireguard')),flush=True)
    print('QUEUE',client.request('/api/wireguard/mode','POST',{'mode':'full_tunnel'})[0],flush=True)
    for sample in range(30):
        state={'network':client.state('/api/network'),'wg':client.state('/api/wireguard')}
        report['samples'].append(state)
        print('SAMPLE',sample,json.dumps(state),flush=True)
        time.sleep(2)
finally:
    try:
        if client.cookie:
            if client.state('/api/network')['revision']:
                print('CANCEL',client.request('/api/network/cancel','POST')[0],flush=True)
            client.request('/api/auth/logout','POST')
    finally:
        client.clear()
        (ROOT/'.tools/hardware/full-mode-diagnosis-red.json').write_text(json.dumps(report,indent=2),encoding='utf8')
