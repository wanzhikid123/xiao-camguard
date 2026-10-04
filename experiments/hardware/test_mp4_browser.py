"""Actual authenticated Chrome H.264/AAC playback of an owned published MP4."""
import json,time
from pathlib import Path
from urllib.parse import quote,parse_qs,urlparse
from playwright.sync_api import sync_playwright
from appliance_client import Client
ROOT=Path(__file__).resolve().parents[2]
def main():
    owned=json.loads((ROOT/'.tools/hardware/conversion-queue/browser-owned.json').read_text())
    if owned.get('testOwned') is not True or not owned['path'].endswith('.mp4'):raise RuntimeError('MP4 ownership guard')
    path=owned['path'];c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt');report={'passed':False,'checks':[],'responses':[]}
    def check(ok,name):
        report['checks'].append({'name':name,'passed':bool(ok)});print('MP4 BROWSER',bool(ok),name,flush=True)
        if not ok:raise RuntimeError(name)
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True))
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe',args=['--no-proxy-server'])
            context=browser.new_context(viewport={'width':390,'height':844})
            context.add_cookies([{'name':'cam_session','value':c.cookie.split('=',1)[1],'url':c.base,'httpOnly':True,'sameSite':'Strict'}]);page=context.new_page()
            def response(item):
                if parse_qs(urlparse(item.url).query).get('path')==[path]:report['responses'].append(item.status)
            page.on('response',response);page.goto(c.base,wait_until='domcontentloaded');page.locator('#dashboard').wait_for(state='visible',timeout=15000)
            card=page.locator('#media-list>li').filter(has_text=path.rsplit('/',1)[1]);card.wait_for(state='visible',timeout=15000);card.get_by_role('button',name='播放 MP4',exact=True).click()
            # wait_for_function internally evals its string, which the device's
            # CSP correctly forbids. Read the native element property instead.
            until=time.monotonic()+30
            while not page.locator('#media-video').evaluate('v=>v.videoWidth>0') and time.monotonic()<until:page.wait_for_timeout(200)
            check(page.locator('#media-video').evaluate('v=>v.videoWidth>0'),'native_video_metadata_available')
            result=page.locator('#media-video').evaluate("async v=>{await v.play();await new Promise(r=>setTimeout(r,1200));return {width:v.videoWidth,height:v.videoHeight,time:v.currentTime,duration:v.duration,error:v.error&&v.error.code}}");report['video']=result
            check(result['width']==320 and result['height']==240 and result['time']>0 and result['duration']>4 and result['error'] is None,'native_h264_aac_playback_and_time_progress')
            page.locator('#media-video').evaluate('v=>{v.currentTime=3}')
            page.wait_for_timeout(1200);check(page.locator('#media-video').evaluate('v=>v.currentTime>=3&&!v.error'),'native_mp4_seeking')
            check(206 in report['responses'],'browser_uses_authenticated_range_response')
            page.locator('#media-video').evaluate('v=>{v.pause();v.removeAttribute("src");v.load()}');browser.close()
        report['passed']=True
    finally:
        time.sleep(1);check(c.request('/api/media/delete','POST',{'path':path})[0]==200,'owned_mp4_cleanup')
        c.clear();(ROOT/'.tools/hardware/mp4-browser-report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if __name__=='__main__':main()
