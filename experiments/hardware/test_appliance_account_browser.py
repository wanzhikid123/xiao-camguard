"""Final account image/UI gate without changing the selected password.

Installs an explicitly supplied image, checks embedded assets and translated
account validation, then submits a synthetic wrong-current-password request.
Retains only empty-input screenshots and public checks; no browser traces.
"""
import argparse
import json
from pathlib import Path
import time
from appliance_client import Client
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--firmware',type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/account-final')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],javascript_errors=0,credentials_or_tokens_saved=False)
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('ACCOUNT-FINAL','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Final account assertion')
    try:
        client.login(progress=lambda phase,seconds:print('ACCOUNT-FINAL login',phase,seconds,flush=True))
        original=client.state('/api/settings/capture')['values'];revision=client.state()['configRevision']
        if args.firmware:report['installation']=client.install(args.firmware)
        check(client.state('/api/settings/capture')['values']==original,'ota_preserves_all_capture_values')
        check(client.state()['configRevision']==revision,'ota_preserves_selected_network_and_verifier_revision')
        for name in ('index.html','app.js','i18n.js','style.css'):
            route='/' if name=='index.html' else '/'+name
            check(client.request(route,authorized=False)[2]==(ROOT/'ui/appliance'/name).read_bytes(),'final_asset_matches_'+name)
        for path,method,raw,headers,expected in (
            ('/api/account/password','POST',b'{}',{'X-CSRF-Token':'','Cookie':''},403),
            ('/api/account?extra=1','GET',None,{},400),
            ('/api/account/password/result?extra=1','GET',None,{},400),
            ('/api/account/password/result','GET',None,{'X-Appliance-Request':'1','X-Password-Change-Ticket':'0'*64},401),
            ('/api/account/password','POST',b'{}',{'Content-Type':'text/plain'},400),
            ('/api/account/password','POST',b'{"currentPassword":"\\u0000","newPassword":"synthetic-valid","confirmPassword":"synthetic-valid"}',{'Content-Type':'application/json'},400),
        ):
            check(client.request(path,method,raw=raw,headers=headers)[0]==expected,'strict_account_request_'+str(len(report['checks'])))
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe',args=['--no-proxy-server'])
            context=browser.new_context(viewport=dict(width=390,height=844),reduced_motion='reduce')
            context.add_cookies([dict(name='cam_session',value=client.cookie.split('=',1)[1],url=client.base,httpOnly=True,sameSite='Strict')])
            page=context.new_page();page.on('pageerror',lambda _:report.update(javascript_errors=report['javascript_errors']+1))
            requests=[]
            page.on('request',lambda request:requests.append(1) if request.url.endswith('/api/account/password') else None)
            page.goto(client.base+'/',wait_until='domcontentloaded');page.locator('#dashboard').wait_for(state='visible',timeout=15000)
            page.locator('#account-settings > summary').click();page.wait_for_timeout(1500)
            # Stop dynamic metric reflow only while taking empty-form images.
            page.evaluate('clearTimeout(polling); polling=null')
            for lang,title in (('cn','账户与安全'),('en','Account and security'),('de','Konto und Sicherheit')):
                page.locator('#language').select_option(lang)
                check(page.locator('#account-settings > summary').inner_text()==title,'translated_security_title_'+lang)
                for width in (390,768,1366):
                    page.set_viewport_size(dict(width=width,height=844));page.wait_for_timeout(300)
                    check(page.evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'final_no_overflow_'+lang+'_'+str(width))
                page.set_viewport_size(dict(width=390,height=844));page.locator('#account-settings').scroll_into_view_if_needed();page.wait_for_timeout(500)
                check(all(page.locator('#account-'+field).input_value()=='' for field in ('current','new','confirm')),'screenshot_has_empty_password_inputs_'+lang)
                page.locator('#account-settings').screenshot(path=str(args.out/('account-'+lang+'.png')),animations='disabled')
            page.locator('#language').select_option('en')
            page.locator('#account-current').fill('synthetic-wrong-current')
            page.locator('#account-new').fill('synthetic-new-password');page.locator('#account-confirm').fill('synthetic-mismatch')
            page.locator('#account-submit').click();page.wait_for_timeout(300)
            check('must match' in page.locator('#account-message').inner_text() and not requests,'browser_mismatch_stops_before_network')
            page.locator('#account-new').fill('short');page.locator('#account-confirm').fill('short');page.locator('#account-submit').click()
            check('10 characters' in page.locator('#account-message').inner_text() and not requests,'browser_policy_stops_before_network')
            page.locator('#account-new').fill('synthetic-new-password');page.locator('#account-confirm').fill('synthetic-new-password')
            started=time.monotonic();page.locator('#account-submit').click();page.wait_for_timeout(300)
            check(all(page.locator('#account-'+field).input_value()=='' for field in ('current','new','confirm')),'final_browser_clears_password_inputs')
            until=time.monotonic()+180
            while time.monotonic()<until and 'current password is incorrect' not in page.locator('#account-message').inner_text():
                page.wait_for_timeout(500)
            check('current password is incorrect' in page.locator('#account-message').inner_text(),'actual_wrong_current_password_message')
            report['rejected_change_seconds']=round(time.monotonic()-started,3)
            check(page.locator('#dashboard').is_visible() and page.locator('#account-submit').is_enabled(),'failed_change_keeps_session_and_restores_button')
            for lang,expected in (('cn','当前密码不正确。'),('de','Das aktuelle Passwort ist falsch.'),('en','The current password is incorrect.')):
                page.locator('#language').select_option(lang);check(page.locator('#account-message').inner_text()==expected,'translated_live_error_'+lang)
            check(len(requests)==1,'only_one_expensive_request_admitted')
            check(page.evaluate("sessionStorage.length===0 && Object.keys(localStorage).every(x=>x==='camera-language')"),'final_no_private_browser_storage')
            check(report['javascript_errors']==0,'final_no_javascript_errors');browser.close()
        check(client.state()['configRevision']==revision,'failed_change_preserves_selected_revision')
        check(client.state('/api/settings/capture')['values']==original,'failed_change_preserves_capture_values')
        check(client.state()['cpuMHz']==240 and client.state()['cameraReady'] and client.state()['sdReady'],'final_stock_clock_camera_and_sd_ready')
        check(client.state('/api/wireguard')['peerSessionUp'],'final_wireguard_session_up')
        report.update(passed=True,selected_revision=revision)
    except Exception as error:
        report['error_type']=type(error).__name__;print('ACCOUNT-FINAL failed',type(error).__name__,flush=True)
    finally:
        if client.cookie:
            try:client.request('/api/auth/logout','POST')
            except Exception:pass
        client.clear();report['check_count']=len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
