"""Real password rotation, cancellation, session revocation and Chrome UI gate.

Uses only ignored local secrets. Rotates through a random recovery password,
then restores the chosen password and all capture values, including on failure.
No cookies, tickets, verifier, private request bodies or browser traces are saved.
"""
import argparse
import json
from pathlib import Path
import secrets
import subprocess
import sys
import time
from urllib.parse import quote
from appliance_client import Client
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from validate_avi import validate


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--firmware',type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/account')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    secret_file=ROOT/'.tools/private/account-test-password.txt'
    # Keep this ignored recovery value if execution is interrupted after commit.
    if not secret_file.exists():secret_file.write_text(secrets.token_urlsafe(32)+'\n',encoding='utf8')
    owner=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    chosen=owner.password;temporary=secret_file.read_text(encoding='utf8').rstrip('\r\n')
    owner.private_values.append(temporary.encode())
    witness=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    witness.private_values.append(temporary.encode())
    report=dict(passed=False,checks=[],rotations=[],status_samples=[],artifact_credentials_or_tokens_saved=False,
                temporary_recovery_password_saved_in_ignored_private_file=True,javascript_errors=0)
    original=None;current=chosen;owned_recording=False;stage='login';browser=None;active_ticket='';pending_target=''
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('ACCOUNT','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Account assertion: '+name)
    def login(client,password):
        client.password=password
        client.login(progress=lambda phase,seconds:print('ACCOUNT login',phase,'seconds',seconds,flush=True))
    def inventory():
        items={};cursor='';dates=[]
        for _ in range(32):
            page=owner.state('/api/media/dates'+('?before='+quote(cursor,safe='') if cursor else ''))
            if page['enumerationTruncated']:raise RuntimeError('Incomplete date enumeration')
            dates.extend(item['date'] for item in page['items']);cursor=page['nextBefore']
            if not cursor:break
        else:raise RuntimeError('Date enumeration bound')
        for date in dates:
            cursor=''
            for _ in range(128):
                page=owner.state('/api/media?date='+date+('&before='+quote(cursor,safe='') if cursor else ''))
                if page['enumerationTruncated']:raise RuntimeError('Incomplete media enumeration')
                items.update({item['path']:item['bytes'] for item in page['items']});cursor=page['nextBefore']
                if not cursor:break
            else:raise RuntimeError('Media enumeration bound')
        return items
    def submit(old,new):
        nonlocal active_ticket,pending_target
        until=time.monotonic()+90
        while True:
            code,headers,body=owner.request('/api/account/password','POST',
                dict(currentPassword=old,newPassword=new,confirmPassword=new))
            if code==429 and time.monotonic()<until:
                print('ACCOUNT waiting for bounded admission',flush=True)
                time.sleep(min(60,int(headers.get('Retry-After','60')))+1);continue
            check(code==202,'password_change_admitted');active_ticket=json.loads(body)['ticket'];pending_target=new;return active_ticket
    def result(ticket,client=owner):
        return client.request('/api/account/password/result',headers={
            'X-Appliance-Request':'1','X-Password-Change-Ticket':ticket},authorized=False)
    def sample_status(elapsed):
        at=time.monotonic();code,_,body=witness.request('/api/status')
        # Commit can occur between a pending-result reply and this probe.
        # Revocation is expected there; never treat it as a network outage.
        if code==401:return
        check(code==200,'responsive_status_during_password_work')
        state=json.loads(body)
        report['status_samples'].append(dict(seconds=round(elapsed,3),latency_s=round(time.monotonic()-at,4),cpu_mhz=state['cpuMHz']))
    def wait_result(ticket,expected,client=owner,sample=True):
        nonlocal active_ticket
        started=time.monotonic();next_probe=0;until=started+560
        while time.monotonic()<until:
            code,_,body=result(ticket,client)
            if code!=202:
                active_ticket='';check(code==expected,'password_change_result_'+str(expected));return json.loads(body)
            elapsed=time.monotonic()-started
            if sample and elapsed>=next_probe:
                next_probe=elapsed+25;sample_status(elapsed)
                print('ACCOUNT password work seconds',round(elapsed),flush=True)
            time.sleep(.7)
        raise RuntimeError('Password change deadline exceeded')
    def update(values):
        check(owner.request('/api/settings/capture','POST',{'values':values},timeout=30)[0]==200,'capture_values_saved')
    try:
        login(owner,chosen);original=owner.state('/api/settings/capture')['values'];baseline=inventory()
        if args.firmware:
            stage='ota';report['installation']=owner.install(args.firmware)
            check(owner.state('/api/settings/capture')['values']==original,'ota_preserves_all_capture_values')
        for name in ('index.html','app.js','i18n.js','style.css'):
            route='/' if name=='index.html' else '/'+name
            check(owner.request(route,authorized=False)[2]==(ROOT/'ui/appliance'/name).read_bytes(),'deployed_asset_'+name)
        check(owner.state()['cameraReady'] and owner.state()['sdReady'],'normal_camera_and_sd_ready')
        login(witness,chosen);initial=owner.state();revision=initial['configRevision']
        check(owner.request('/api/account',authorized=False)[0]==401,'account_read_requires_session')
        check(owner.state('/api/account')==dict(adminConfigured=True,passwordChangePending=False,minimumCharacters=10,maximumBytes=128),'account_policy_public_allowlist')
        valid=dict(currentPassword=chosen,newPassword=temporary,confirmPassword=temporary)
        for fields,name in (({'X-CSRF-Token':''},'csrf'),({'Origin':'http://untrusted.invalid'},'origin')):
            check(owner.request('/api/account/password','POST',valid,headers=fields)[0]==403,'change_requires_'+name)
        invalid=[{},dict(valid,extra='x'),dict(valid,currentPassword=''),dict(valid,newPassword='x',confirmPassword='x'),
                 dict(valid,confirmPassword='mismatch'),dict(valid,newPassword=chosen,confirmPassword=chosen),
                 dict(valid,newPassword='x'*129,confirmPassword='x'*129),dict(valid,newPassword='密'*43,confirmPassword='密'*43),
                 dict(valid,newPassword='valid-password\0',confirmPassword='valid-password\0'),dict(valid,currentPassword={'nested':'x'})]
        for i,value in enumerate(invalid):check(owner.request('/api/account/password','POST',value)[0]==400,'invalid_change_rejected_'+str(i))
        check(owner.request('/api/account/password/result',authorized=False)[0]==401,'result_requires_private_ticket')
        stage='cancellation';ticket=submit(chosen,temporary)
        check(owner.request('/api/auth/logout','POST')[0]==200,'logout_during_password_work')
        body=wait_result(ticket,409);check(body['error']=='password_change_cancelled','originating_logout_cancels_change')
        check(witness.state()['configRevision']==revision,'cancelled_change_preserves_revision')
        login(owner,chosen);check(True,'original_password_valid_after_cancel')
        stage='wrong_current';ticket=submit(secrets.token_urlsafe(24),temporary)
        check(wait_result(ticket,401)['error']=='invalid_credentials','wrong_current_password_rejected')
        check(owner.state()['configRevision']==revision,'wrong_current_preserves_revision')
        stage='browser';
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe',args=['--no-proxy-server'])
            context=browser.new_context(viewport=dict(width=390,height=844),reduced_motion='reduce')
            context.add_cookies([dict(name='cam_session',value=owner.cookie.split('=',1)[1],url=owner.base,httpOnly=True,sameSite='Strict')])
            page=context.new_page();page.on('pageerror',lambda _:report.update(javascript_errors=report['javascript_errors']+1))
            def private_admission(response):
                nonlocal active_ticket,pending_target
                if response.url.endswith('/api/account/password') and response.status==202:
                    active_ticket=response.json()['ticket'];pending_target=temporary
            page.on('response',private_admission)
            page.goto(owner.base+'/',wait_until='domcontentloaded');page.locator('#dashboard').wait_for(state='visible',timeout=15000)
            page.locator('#account-settings > summary').click()
            for lang in ('cn','en','de'):
                page.locator('#language').select_option(lang)
                for width in (390,768,1366):
                    page.set_viewport_size(dict(width=width,height=844));page.wait_for_timeout(150)
                    check(page.evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'account_no_overflow_'+lang+'_'+str(width))
                check(all(page.locator('#account-'+field).get_attribute('type')=='password' for field in ('current','new','confirm')),'all_password_fields_masked_'+lang)
                page.set_viewport_size(dict(width=390,height=844));page.locator('#account-settings').screenshot(path=str(args.out/('account-'+lang+'.png')))
            page.locator('#language').select_option('en')
            page.locator('#account-current').fill(chosen);page.locator('#account-new').fill(temporary);page.locator('#account-confirm').fill(temporary)
            started=time.monotonic();page.locator('#account-submit').click();page.wait_for_timeout(500)
            check(all(page.locator('#account-'+field).input_value()=='' for field in ('current','new','confirm')),'browser_clears_all_password_fields_during_work')
            check(page.locator('#account-submit').is_disabled(),'browser_prevents_duplicate_change')
            check(owner.state('/api/account')['passwordChangePending'],'real_browser_change_queued')
            check(owner.request('/api/wireguard/restart','POST')[0]==409,'network_change_excluded_during_password_work')
            check(owner.request('/api/account/password','POST',valid)[0]==409,'second_password_change_excluded')
            check(owner.request('/api/auth/login','POST',{'password':chosen},headers={'X-Appliance-Request':'1'})[0]==503,'login_shares_bounded_password_queue')
            image=(args.firmware or ROOT/'.tools/hardware/appliance-account-candidate.bin').read_bytes()
            import hashlib
            check(owner.request('/api/ota','POST',raw=image[:288],headers={'Content-Type':'application/octet-stream','X-Firmware-SHA256':hashlib.sha256(image).hexdigest()})[0]==409,'ota_excluded_during_password_work')
            code,_,jpeg=owner.request('/api/camera/still',timeout=30)
            check(code==200 and jpeg.startswith(b'\xff\xd8') and jpeg.endswith(b'\xff\xd9'),'fresh_camera_frame_during_password_work')
            before=inventory();owned_recording=owner.request('/api/record/start','POST')[0]==202;check(owned_recording,'record_during_password_work_accepted')
            until=time.monotonic()+15
            while time.monotonic()<until and not owner.state()['recording']:time.sleep(.3)
            check(owner.state()['recording'] and owner.state()['audioInputRunning'],'real_pdm_recording_during_password_work')
            page.wait_for_timeout(8000);check(owner.request('/api/record/stop','POST')[0]==202,'record_stop_during_password_work')
            until=time.monotonic()+25
            while time.monotonic()<until and owner.state()['recording']:time.sleep(.3)
            check(not owner.state()['recording'] and not owner.state()['recordRequested'],'concurrent_record_finalized');owned_recording=False
            added=[path for path in inventory() if path not in before and path.endswith('.avi')];check(len(added)==1,'one_owned_concurrent_video')
            target=args.out/'password-work.avi';owner.download('/api/media/download?path='+quote(added[0],safe=''),target)
            probe=json.loads(subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error','-show_streams','-show_format','-of','json',str(target)],capture_output=True,text=True,check=True,timeout=30).stdout)
            media=validate(probe);decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(target),'-f','null','-'],capture_output=True,timeout=45)
            check(media['passed'] and media['duration_comparison_available'] and decoded.returncode==0,'whole_concurrent_mjpeg_pcm_decode')
            report['concurrent_clip']=dict(path=added[0],bytes=target.stat().st_size,durations_s=media['durations_s'],difference_s=media['duration_difference_s'])
            until=started+200;next_probe=0
            while time.monotonic()<until and not page.locator('#login-panel').is_visible():
                elapsed=time.monotonic()-started
                if elapsed>=next_probe:
                    next_probe=elapsed+25;sample_status(elapsed)
                    print('ACCOUNT Chrome password work seconds',round(elapsed),flush=True)
                page.wait_for_timeout(500)
            check(page.locator('#login-panel').is_visible(),'browser_returns_to_login_after_change')
            until=time.monotonic()+5
            while time.monotonic()<until and 'Password changed' not in page.locator('#login-message').inner_text():page.wait_for_timeout(150)
            check('Password changed' in page.locator('#login-message').inner_text(),'browser_reports_success_after_cookie_revocation')
            current=temporary;active_ticket='';report['rotations'].append(dict(clock=240,seconds=round(time.monotonic()-started,3)))
            check(page.evaluate("sessionStorage.length===0 && Object.keys(localStorage).every(x=>x==='camera-language')"),'no_password_or_ticket_browser_storage')
            check(report['javascript_errors']==0,'no_browser_javascript_errors');browser.close();browser=None
        check(owner.request('/api/status')[0]==401 and witness.request('/api/status')[0]==401,'all_previous_sessions_revoked')
        stage='temporary_restart';login(owner,temporary)
        check(owner.state()['configRevision']>revision,'verifier_commit_advances_revision')
        check(owner.state('/api/settings/capture')['values']==original,'all_values_preserved_by_password_commit')
        check(owner.state('/api/wireguard')['peerSessionUp'],'wireguard_preserved_by_password_commit')
        check(owner.request('/api/device/restart','POST')[0]==202,'normal_restart_with_temporary_password');owner.wait_boot();login(owner,temporary)
        check(True,'temporary_password_survives_normal_restart')
        stage='low_clock_restore';update({'cpuMHz':80});login(witness,temporary)
        old_revision=owner.state()['configRevision'];ticket=submit(temporary,chosen);started=time.monotonic()
        check(result('0'*64)[0]==401,'wrong_change_ticket_rejected')
        body=wait_result(ticket,200);current=chosen
        check(body==dict(changed=True,signInRequired=True),'low_clock_change_returns_only_public_success')
        report['rotations'].append(dict(clock=80,seconds=round(time.monotonic()-started,3)))
        check(result(ticket)[0]==401,'completed_change_ticket_is_single_use')
        check(owner.request('/api/status')[0]==401 and witness.request('/api/status')[0]==401,'low_clock_change_revokes_all_sessions')
        login(owner,chosen);check(owner.state()['cpuMHz']==80,'chosen_password_login_at_low_clock_after_change')
        check(owner.state()['configRevision']>old_revision,'low_clock_revision_committed')
        update(original);check(owner.request('/api/device/restart','POST')[0]==202,'normal_restart_after_password_restoration');owner.wait_boot();login(owner,chosen)
        check(owner.state('/api/settings/capture')['values']==original,'all_original_values_after_restart')
        check(owner.state()['cpuMHz']==original['cpuMHz'] and owner.state()['sdReady'] and owner.state()['cameraReady'],'original_clock_camera_sd_after_restart')
        check(owner.state('/api/wireguard')['peerSessionUp'],'wireguard_after_password_restore_restart')
        final=inventory();check(all(final.get(path)==size for path,size in baseline.items()),'all_preexisting_media_preserved')
        report.update(passed=True,preexisting_media_count=len(baseline),maximum_status_latency_s=max(x['latency_s'] for x in report['status_samples']))
    except Exception as error:
        report.update(stage=stage,error_type=type(error).__name__);print('ACCOUNT failed at',stage,'type',type(error).__name__,flush=True)
    finally:
        if browser:browser.close()
        try:
            # Settle admitted work before cleanup. A lost response or closed
            # page cannot leave a later commit racing the password restoration.
            if active_ticket:
                until=time.monotonic()+560
                while time.monotonic()<until:
                    code,_,body=result(active_ticket)
                    if code==202:time.sleep(.7);continue
                    if code==200 and json.loads(body).get('changed'):current=pending_target
                    active_ticket='';break
                else:raise RuntimeError('Outstanding password work did not settle')
            if current!=chosen or not owner.cookie or owner.request('/api/status')[0]!=200:
                # A response can be lost after a successful commit. Try both
                # private candidates without saving their values or cookies.
                try:login(owner,current)
                except Exception:
                    current=temporary if current==chosen else chosen;login(owner,current)
            if owned_recording:owner.request('/api/record/stop','POST');time.sleep(3)
            if current!=chosen:
                ticket=submit(current,chosen);wait_result(ticket,200,sample=False);current=chosen;login(owner,chosen)
            if original is not None:update(original)
            check(owner.state('/api/settings/capture')['values']==original,'final_original_capture_values')
            check(current==chosen,'final_chosen_password_restored');owner.request('/api/auth/logout','POST')
        except Exception as error:report.update(passed=False,restore_error_type=type(error).__name__)
        owner.clear();witness.clear();chosen=temporary=current=active_ticket=pending_target='';report['check_count']=len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
