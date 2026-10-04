"""Repeated actual public probes with heap samples; cancel the unpromoted mode."""
import argparse
import json
import time
from appliance_client import Client
from full_mode_tools import ROOT

parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--label',required=True)
parser.add_argument('--reset-before',action='store_true')
args=parser.parse_args();client=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
report={'hypotheses':['TLS objects survive vTaskDelete and leak buffers','generic resolver chooses unsupported IPv6','public service or DNS availability','route restoration or stale generation'],'samples':[]}
def sample(label):
    state=client.state();wg=client.state('/api/wireguard')
    item={'label':label,'freeHeap':state['freeHeap'],'minimumFreeHeap':state['minimumFreeHeap'],'verified':wg['fullTunnelVerified'],'http':wg['routeProbeHTTP'],'sent':wg['sentBytes'],'received':wg['receivedBytes']}
    report['samples'].append(item);print('PROBE-MEM',json.dumps(item),flush=True);return wg
try:
    client.login()
    if args.reset_before:
        if client.state()['recording']:raise RuntimeError('Cannot reset during recording')
        if client.request('/api/device/restart','POST')[0]!=202:raise RuntimeError('Reset refused')
        client.wait_boot();client.login()
    sample('before')
    report['queue_http']=client.request('/api/wireguard/mode','POST',{'mode':'full_tunnel'})[0]
    deadline=time.monotonic()+55
    while time.monotonic()<deadline:
        network=client.state('/api/network')
        if network['state']=='awaiting_local_confirmation':break
        time.sleep(.3)
    if network['state']!='awaiting_local_confirmation':raise RuntimeError('Initial full mode not ready')
    if client.request('/api/network/confirm','POST',{'revision':network['revision']})[0]!=202:raise RuntimeError('Initial confirmation refused')
    time.sleep(1)
    for iteration in range(4):
        deadline=time.monotonic()+23;seen_busy=False
        while time.monotonic()<deadline:
            wg=client.state('/api/wireguard');seen_busy=seen_busy or wg['routeProbeBusy']
            if wg['fullTunnelVerified'] or (seen_busy and not wg['routeProbeBusy']):break
            time.sleep(.25)
        sample('public_probe_'+str(iteration))
        if iteration<3:
            if client.request('/api/wireguard/restart','POST')[0]!=202:raise RuntimeError('Probe restart not admitted')
            time.sleep(1)
    report['heap_drop']=report['samples'][0]['freeHeap']-report['samples'][-1]['freeHeap']
    report['all_probes_verified']=all(item['verified'] for item in report['samples'][1:])
finally:
    try:
        if client.cookie:
            if client.state('/api/network')['revision']:
                client.request('/api/network/cancel','POST');time.sleep(1)
            if client.state('/api/wireguard')['mode']=='full_tunnel':
                client.request('/api/wireguard/mode','POST',{'mode':'remote_access'});deadline=time.monotonic()+55
                while time.monotonic()<deadline:
                    network=client.state('/api/network')
                    if network['state']=='awaiting_local_confirmation':break
                    time.sleep(.3)
                if network['state']=='awaiting_local_confirmation':
                    client.request('/api/network/confirm','POST',{'revision':network['revision']});time.sleep(1)
            client.request('/api/auth/logout','POST')
    finally:
        client.clear();(ROOT/'.tools/hardware'/('full-probe-memory-'+args.label+'.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
