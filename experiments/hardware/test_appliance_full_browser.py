"""Final production full-mode UI/media gate; leave confirmed full mode active."""
import argparse
import json
from pathlib import Path
import time
from urllib.parse import quote
from appliance_client import Client
from full_mode_tools import ROOT,expected_exit,inventory,decode_jpeg,decode_avi
from playwright.sync_api import sync_playwright


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--address',required=True)
    parser.add_argument('--firmware',type=Path);parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/full-final-browser')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    client=Client(args.address,ROOT/'.tools/private/admin-password.txt');expected=expected_exit(client)
    report=dict(passed=False,checks=[],javascript_errors=0,credentials_or_tokens_saved=False);owned_record=False
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)));print('FULL-FINAL','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Final full mode assertion: '+name)
    def wait(predicate,path='/api/status',seconds=65):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            state=client.state(path)
            if predicate(state):return state
            time.sleep(.3)
        report['wait_failure']={'path':path,'last_state':state};raise RuntimeError('Final full mode wait deadline')
    try:
        client.login();values=client.state('/api/settings/capture')['values'];old_media=inventory(client)
        if args.firmware:report['installation']=client.install(args.firmware)
        for name in ('index.html','app.js','i18n.js','style.css'):
            check(client.request('/' if name=='index.html' else '/'+name,authorized=False)[2]==(ROOT/'ui/appliance'/name).read_bytes(),
                  'embedded_asset_matches_'+name)
        check(client.request('/api/wireguard/policy-test')[0]==404 and client.request('/api/wireguard/full-test')[0]==404,'no_hardware_test_routes_in_normal_image')
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe',args=['--no-proxy-server'])
            context=browser.new_context(viewport=dict(width=390,height=844),reduced_motion='reduce')
            context.add_cookies([dict(name='cam_session',value=client.cookie.split('=',1)[1],url=client.base,httpOnly=True,sameSite='Strict')])
            page=context.new_page();page.on('pageerror',lambda _:report.update(javascript_errors=report['javascript_errors']+1))
            page.goto(client.base+'/',wait_until='domcontentloaded');page.locator('#dashboard').wait_for(state='visible',timeout=15000)
            page.locator('details').filter(has=page.locator('#wg-mode-form')).locator('summary').first.click()
            page.wait_for_timeout(1500)
            for lang,title in (('cn','连接模式'),('en','Connection mode'),('de','Verbindungsmodus')):
                page.locator('#language').select_option(lang);page.wait_for_timeout(400)
                check(page.locator('label[for="wg-mode"]').inner_text()==title,'translated_mode_label_'+lang)
                for width in (390,768,1366):
                    page.set_viewport_size(dict(width=width,height=844));page.wait_for_timeout(250)
                    check(page.evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'no_overflow_'+lang+'_'+str(width))
                page.set_viewport_size(dict(width=390,height=844));page.locator('#wg-mode-form').scroll_into_view_if_needed()
                check(page.locator('#wg-config').input_value()=='' and all(page.locator('#wg-'+field).input_value()=='' for field in ('private-key','public-key','preshared-key')),
                      'screenshot_contains_no_keys_'+lang)
                page.screenshot(path=str(args.out/('wireguard-'+lang+'.png')),animations='disabled')
            page.locator('#language').select_option('en')
            if client.state('/api/wireguard')['mode']!='full_tunnel':
                page.locator('#wg-mode').select_option('full_tunnel');page.wait_for_timeout(3000)
                check(page.locator('#wg-mode').input_value()=='full_tunnel','polling_preserves_user_mode_choice')
                page.locator('#wg-mode-submit').click()
                network=wait(lambda state:state['state']=='awaiting_local_confirmation','/api/network')
                page.wait_for_timeout(3000)
                check('Full-tunnel exit verified' in page.locator('#wg-message').inner_text(),'actual_translated_verified_full_state')
                check(page.locator('#network-confirm').is_visible(),'lan_browser_offers_trial_confirmation')
                page.locator('#network-confirm').click();wait(lambda state:not state['revision'] and state['state']=='ready','/api/network')
                check(True,'actual_browser_promotes_verified_full_mode')
            for lang,phrase in (('de','Internetausgang'),('cn','公网出口'),('en','Internet exit')):
                page.locator('#language').select_option(lang);page.wait_for_timeout(3000)
                check(phrase in page.locator('#wg-route').inner_text(),'translated_live_public_exit_'+lang)
                page.locator('#wg-mode-form').scroll_into_view_if_needed()
                page.screenshot(path=str(args.out/('verified-full-'+lang+'.png')),animations='disabled')
            check(page.evaluate("sessionStorage.length===0 && Object.keys(localStorage).every(x=>x==='camera-language')"),'no_private_browser_storage')
            check(report['javascript_errors']==0,'no_javascript_errors');browser.close()
        wg=wait(lambda state:state['fullTunnelVerified'],'/api/wireguard');report['final_wireguard']=wg
        check(wg['guardReady'] and wg['publicExit'] in expected,'final_expected_server_exit_and_guard')
        code,_,body=client.request('/api/camera/still',timeout=30)
        check(code==200 and decode_jpeg(body,args.out/'full-still.jpg'),'final_full_mode_jpeg_decode')
        check(client.request('/api/camera/photo','POST')[0]==201,'final_full_mode_photo_saved')
        check(client.request('/api/record/start','POST')[0]==202,'final_full_mode_record_start');owned_record=True
        wait(lambda state:state['recording'] and state['audioInputRunning'],seconds=10);time.sleep(8)
        check(client.request('/api/record/stop','POST')[0]==202,'final_full_mode_record_stop')
        wait(lambda state:not state['recording'] and not state['recordRequested'],seconds=15);owned_record=False
        media=inventory(client);clips=[path for path in media if path not in old_media and path.endswith('.avi')]
        photos=[path for path in media if path not in old_media and path.endswith('.jpg')]
        check(len(clips)==1 and len(photos)==1,'exactly_one_owned_clip_and_photo')
        target=args.out/'full-mode.avi';check(client.download('/api/media/download?path='+quote(clips[0],safe=''),target)==media[clips[0]],'whole_audio_clip_readback')
        avi=decode_avi(target);report['recording']=avi;check(avi['passed'] and avi['whole_decode_passed'],'whole_final_full_mode_mjpeg_pcm_decode')
        target=args.out/'full-photo.jpg';check(client.download('/api/media/download?path='+quote(photos[0],safe=''),target)==media[photos[0]],'whole_photo_readback')
        check(decode_jpeg(target.read_bytes(),target),'whole_saved_photo_decode')
        check(all(media.get(path)==size for path,size in old_media.items()),'all_existing_media_preserved')
        check(client.state('/api/settings/capture')['values']==values and client.state()['cpuMHz']==240,'all_31_values_and_stock_clock_preserved')
        check(client.state()['cameraReady'] and client.state()['sdReady'] and client.state('/api/wireguard')['fullTunnelVerified'],'final_camera_sd_and_full_tunnel_healthy')
        report.update(passed=True,original_media_count=len(old_media),selected_revision=client.state()['configRevision'])
    except Exception as error:
        report['passed']=False;report['error_type']=type(error).__name__;print('FULL-FINAL FAIL',type(error).__name__,flush=True)
    finally:
        try:
            if owned_record:
                client.request('/api/record/stop','POST');wait(lambda state:not state['recording'] and not state['recordRequested'],seconds=15)
            if client.cookie:
                if client.state('/api/network')['revision']:
                    client.request('/api/network/cancel','POST');wait(lambda state:not state['revision'],'/api/network')
                client.request('/api/auth/logout','POST')
        except Exception as error:report['passed']=False;report['cleanup_error_type']=type(error).__name__
        client.clear();report['check_count']=len(report['checks']);(args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
