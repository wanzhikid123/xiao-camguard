"""Real Chrome feature UI acceptance; delete only this run's own session."""
import json,time
from pathlib import Path
from playwright.sync_api import sync_playwright,expect
from appliance_client import Client
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'.tools/hardware/feature-browser'
def main():
    OUT.mkdir(parents=True,exist_ok=True);c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt')
    report={'passed':False,'checks':[],'javascriptErrors':0,'privateValuesSaved':False};owned=None
    def check(ok,name):
        report['checks'].append({'name':name,'passed':bool(ok)});print('BROWSER',bool(ok),name,flush=True)
        if not ok:raise RuntimeError(name)
    def wait(predicate,seconds=30):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            state=c.state()
            if predicate(state):return state
            time.sleep(.3)
        raise RuntimeError('Browser feature state deadline')
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True))
        assets=('index.html','app.js','i18n.js','style.css')
        matches=all(c.request('/' if name=='index.html' else '/'+name,authorized=False)[2]==(ROOT/'ui/appliance'/name).read_bytes() for name in assets)
        if not matches:
            check(c.request('/api/codecs/test')[0]==200,'asset_upgrade_requires_isolated_fixture')
            report['installation']=c.install(ROOT/'.pio/build/xiao-appliance-codec-test/firmware.bin')
        for name in assets:
            path='/' if name=='index.html' else '/'+name
            check(c.request(path,authorized=False)[2]==(ROOT/'ui/appliance'/name).read_bytes(),'installed_asset_'+name)
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe',args=['--no-proxy-server'])
            context=browser.new_context(viewport={'width':390,'height':844},reduced_motion='reduce')
            context.add_cookies([{'name':'cam_session','value':c.cookie.split('=',1)[1],'url':c.base,'httpOnly':True,'sameSite':'Strict'}])
            page=context.new_page();page.on('pageerror',lambda _:report.update(javascriptErrors=report['javascriptErrors']+1))
            page.goto(c.base,wait_until='domcontentloaded');page.locator('#dashboard').wait_for(state='visible',timeout=15000)
            page.locator('#timelapse-panel').evaluate('(el)=>el.open=true')
            page.locator('#ble-panel').evaluate('(el)=>el.open=true')
            for lang in ('cn','en','de'):
                page.locator('#language').select_option(lang);page.wait_for_timeout(400)
                expect(page.locator('#tl-session-details')).to_have_text({'cn':'暂无拍照文件夹','en':'No photo folders yet','de':'Noch keine Fotoordner'}[lang],timeout=10000)
                check(True,'localized_empty_photo_folder_'+lang)
                for width in (390,768,1366):
                    page.set_viewport_size({'width':width,'height':900});page.wait_for_timeout(150)
                    check(page.evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'no_horizontal_overflow_'+lang+'_'+str(width))
                page.set_viewport_size({'width':390,'height':900});page.locator('#timelapse-panel').scroll_into_view_if_needed()
                page.screenshot(path=str(OUT/(lang+'.png')),animations='disabled')
            page.locator('#language').select_option('en')
            page.locator('#tl-interval').fill('5');page.locator('#tl-immediate').check();page.locator('#tl-start').click()
            state=wait(lambda s:s['timelapse']['active']);owned=state['timelapse']['session']
            wait(lambda s:s['timelapse']['photos']>=1)
            page.wait_for_timeout(3000)
            check(page.locator('#event-start').is_disabled(),'event_start_disabled_during_interval_session')
            check(page.locator('#capture-submit').is_disabled(),'capture_settings_locked_in_ui')
            page.locator('#tl-refresh').click();expect(page.locator('#tl-files a')).not_to_have_count(0,timeout=10000)
            check(page.locator('#tl-delete').is_disabled(),'running_folder_delete_disabled')
            page.locator('#tl-stop').click();wait(lambda s:not s['timelapse']['active']);page.wait_for_timeout(3000)
            page.locator('#tl-refresh').click();page.wait_for_timeout(800)
            check(not page.locator('#tl-delete').is_disabled(),'stopped_folder_deletable_in_ui')
            page.once('dialog',lambda d:d.dismiss());page.locator('#tl-delete').click();page.wait_for_timeout(500)
            check(any(x['name']==owned for x in c.state('/api/timelapse/sessions')['items']),'cancel_keeps_owned_folder')
            page.once('dialog',lambda d:d.accept());page.locator('#tl-delete').click();page.wait_for_timeout(1500)
            check(not any(x['name']==owned for x in c.state('/api/timelapse/sessions')['items']),'confirmed_delete_removes_owned_folder');owned=None
            # Hold a real authenticated PIN response in RAM across sign-out.
            pending=[]
            page.route('**/api/settings/ble',lambda route:pending.append((route,route.fetch())))
            # Display one response first, so a stale translation binding is
            # also covered when language changes after signing out.
            page.locator('#ble-show').click();page.wait_for_timeout(600)
            check(bool(pending),'delayed_authenticated_pin_response_captured_in_ram')
            route,response=pending.pop();route.fulfill(response=response);page.wait_for_timeout(300)
            check(bool(page.locator('#ble-message').text_content()),'authenticated_pin_displayed_without_saving_it')
            page.locator('#ble-show').click();page.wait_for_timeout(300)
            page.locator('#logout').click();page.locator('#login-panel').wait_for(state='visible')
            for route,response in pending:route.fulfill(response=response)
            pending.clear();page.wait_for_timeout(500)
            check(page.locator('#ble-message').text_content()=='','late_pin_response_hidden_after_logout')
            page.locator('#language').select_option('de');page.wait_for_timeout(300)
            check(page.locator('#ble-message').text_content()=='','language_change_does_not_restore_cleared_pin')
            check(page.evaluate("sessionStorage.length===0&&Object.keys(localStorage).every(x=>x==='camera-language')"),'private_values_not_persisted_in_browser')
            check(report['javascriptErrors']==0,'no_javascript_errors');browser.close()
        report['passed']=True
    finally:
        if owned:
            c.request('/api/timelapse/stop','POST');time.sleep(3);c.request('/api/timelapse/delete','POST',{'session':owned})
        c.clear();(OUT/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if __name__=='__main__':main()
