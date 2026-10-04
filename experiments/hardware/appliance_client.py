"""Private-in-RAM hardware HTTP client shared by product acceptance scripts."""
import hashlib
from contextlib import closing
import http.client
import json
from pathlib import Path
import select
import time
from urllib.error import HTTPError
from urllib.request import Request
import baseline_http as transport


class Client:
    def __init__(self,address,password_file):
        self.base='http://'+address
        self.password=Path(password_file).read_text(encoding='utf-8').rstrip('\r\n')
        self.cookie=self.csrf=''; self.private_values=[self.password.encode()]
        self.maximum_latency=0

    def request(self,path,method='GET',value=None,raw=None,headers=None,authorized=True,timeout=10):
        fields={'Cookie':self.cookie} if self.cookie and authorized else {}
        if method=='POST': fields.update({'X-CSRF-Token':self.csrf,'Origin':self.base})
        if value is not None: raw=json.dumps(value).encode(); fields['Content-Type']='application/json'
        fields.update(headers or {}); started=time.monotonic()
        try: response=transport.HTTP.open(Request(self.base+path,data=raw,headers=fields,method=method),timeout=timeout)
        except HTTPError as error: response=error
        with response:
            body=response.read(250000)
            if any(secret and secret in body for secret in self.private_values): raise RuntimeError('Private value in HTTP response')
            self.maximum_latency=max(self.maximum_latency,time.monotonic()-started)
            return response.code,dict(response.headers),body

    def state(self,path='/api/status'):
        code,_,body=self.request(path)
        if code!=200: raise RuntimeError('State endpoint unavailable: '+path)
        return json.loads(body)

    def login(self,result_timeout=240,progress=None):
        self.cookie=self.csrf=''
        started=time.monotonic();last_progress=-30
        def report_wait(phase):
            nonlocal last_progress
            elapsed=time.monotonic()-started
            if progress and elapsed-last_progress>=30:
                last_progress=elapsed;progress(phase,round(elapsed))
        admission_deadline=time.monotonic()+240
        while True:
            code,headers,body=self.request('/api/auth/login','POST',{'password':self.password},headers={'X-Appliance-Request':'1'})
            if code==429 and time.monotonic()<admission_deadline:
                report_wait('rate_limit')
                time.sleep(min(60,int(headers.get('Retry-After','59')))+1);continue
            if code==503 and json.loads(body).get('error')=='login_busy' and time.monotonic()<admission_deadline:
                report_wait('prior_login_job')
                time.sleep(3);continue
            break
        if code!=202: raise RuntimeError('Chosen-account login not admitted')
        ticket=json.loads(body)['ticket']; deadline=time.monotonic()+result_timeout
        while time.monotonic()<deadline:
            code,headers,body=self.request('/api/auth/result',headers={'X-Login-Ticket':ticket})
            if code==202:
                report_wait('password_work');time.sleep(.7);continue
            if code!=200:
                try:reason=json.loads(body).get('error','unspecified')
                except ValueError:reason='invalid_response'
                raise RuntimeError('Chosen-account login not verified: '+str(code)+' '+str(reason))
            self.cookie=headers['Set-Cookie'].split(';',1)[0]; self.csrf=json.loads(body)['csrf']; return
        raise RuntimeError('Chosen-account login timed out')

    def wait_boot(self):
        time.sleep(4); deadline=time.monotonic()+65
        while time.monotonic()<deadline:
            try:
                code,_,body=self.request('/api/bootstrap',authorized=False,timeout=3)
                if code==200 and json.loads(body)['adminConfigured']:
                    time.sleep(3); return
            except (OSError,ValueError,http.client.HTTPException): pass
            time.sleep(.5)
        raise RuntimeError('Board HTTP did not recover')

    def install(self,firmware):
        image=Path(firmware).read_bytes(); digest=hashlib.sha256(image).hexdigest()
        # Bound each write and allow the async receiver to validate the prefix.
        # A single multi-megabyte send can hide an early rejection behind an
        # OS connection reset; do not resend or assume that firmware was saved.
        with closing(http.client.HTTPConnection(self.base.removeprefix('http://'),timeout=100)) as connection:
            connection.putrequest('POST','/api/ota')
            for name,value in {'Cookie':self.cookie,'Origin':self.base,'X-CSRF-Token':self.csrf,
                               'Content-Type':'application/octet-stream','X-Firmware-SHA256':digest,
                               'Content-Length':str(len(image))}.items():connection.putheader(name,value)
            connection.endheaders();sent=0
            for start in range(0,len(image),4096):
                if select.select([connection.sock],[],[],0)[0]:break
                chunk=image[start:start+4096];connection.send(chunk);sent+=len(chunk)
                if start==0:time.sleep(.5)
            response=connection.getresponse();body=response.read(1000)
            if any(secret and secret in body for secret in self.private_values):raise RuntimeError('Private OTA response')
            if sent!=len(image) or response.status!=200 or not json.loads(body).get('updated'):
                raise RuntimeError('Authenticated OTA installation failed')
        self.wait_boot(); self.login()
        return {'bytes':len(image),'sha256':digest}

    def download(self,path,output):
        with transport.HTTP.open(Request(self.base+path,headers={'Cookie':self.cookie}),timeout=10) as response, Path(output).open('wb') as file:
            if response.code!=200: raise RuntimeError('Download refused')
            while chunk:=response.read(65536): file.write(chunk)
        return Path(output).stat().st_size

    def clear(self):
        if self.cookie:
            try:self.request('/api/auth/logout','POST')
            except Exception:pass
        self.password=self.cookie=self.csrf=''; self.private_values=[]
