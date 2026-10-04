"""Read-only busy snapshots, then bounded staged authenticated OTA transport."""
import argparse
import hashlib
import http.client
import json
from pathlib import Path
import select
import time
from appliance_client import Client
from full_mode_tools import ROOT

parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--firmware',type=Path,required=True)
args=parser.parse_args();client=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
report={'hypotheses':['capture standby transition rejects an immediate upload','pending network trial rejects upload','task/heap or stream quiescence prevents OTA','image prefix invalid','transport send hides early HTTP rejection'],'samples':[]}
try:
    client.login(progress=lambda phase,s:print('OTA-DIAG login',phase,s,flush=True))
    for i in range(3):
        state={'device':client.state(),'network':client.state('/api/network'),'ota':client.state('/api/ota')}
        report['samples'].append(state);print('STATE',json.dumps(state),flush=True);time.sleep(1)
    image=args.firmware.read_bytes();connection=http.client.HTTPConnection('192.168.1.181',80,timeout=100)
    connection.putrequest('POST','/api/ota');connection.putheader('Cookie',client.cookie)
    connection.putheader('Origin',client.base);connection.putheader('X-CSRF-Token',client.csrf)
    connection.putheader('Content-Type','application/octet-stream');connection.putheader('X-Firmware-SHA256',hashlib.sha256(image).hexdigest())
    connection.putheader('Content-Length',str(len(image)));connection.endheaders()
    sent=0
    for start in range(0,len(image),4096):
        if select.select([connection.sock],[],[],0)[0]:break
        chunk=image[start:start+4096];connection.send(chunk);sent+=len(chunk)
        if start==0:time.sleep(.5)
    response=connection.getresponse();body=response.read(1000)
    if any(secret in body for secret in client.private_values):raise RuntimeError('Private response')
    report['upload']={'sent_bytes':sent,'http':response.status,'body':json.loads(body)}
    print('UPLOAD',json.dumps(report['upload']),flush=True);connection.close()
    if response.status==200:
        client.wait_boot();client.login();print('AFTER',json.dumps(client.state('/api/wireguard')),flush=True)
except Exception as error:
    report['error_type']=type(error).__name__;print('OTA-DIAG FAIL',type(error).__name__,flush=True)
finally:
    if client.cookie:
        try:client.request('/api/auth/logout','POST')
        except Exception:pass
    client.clear();(ROOT/'.tools/hardware/full-ota-diagnosis.json').write_text(json.dumps(report,indent=2),encoding='utf8')
