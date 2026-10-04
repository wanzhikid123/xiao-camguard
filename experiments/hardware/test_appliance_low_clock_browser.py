"""Real Chrome login at 80 MHz, with an independent RAM recovery session.

Optional authenticated OTA first. No password/cookie/ticket is recorded. The
original capture settings are restored even when browser login times out.
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
    parser.add_argument('--sensor-standby',action='store_true')
    parser.add_argument('--restore-cpu',type=int,choices=(80,160,240),
                        help='Explicit recovery clock after an interrupted prior gate; other values are preserved')
    parser.add_argument('--fallback-address',help='Explicitly known address of the same board, only for restoring values')
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/low-clock-browser')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    owner=Client(args.address,ROOT/'.tools/private/admin-password.txt')
    report=dict(passed=False,checks=[],samples=[],javascript_errors=0,credentials_or_tokens_saved=False)
    original=None;stage='owner_login'
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print('LOW-CLOCK-UI','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Low clock browser assertion')
    try:
        owner.login(progress=lambda phase,seconds:print('LOW-CLOCK-UI owner',phase,'seconds',seconds,flush=True))
        original=owner.state('/api/settings/capture')['values']
        saved_before=dict(original)
        if args.restore_cpu is not None:original['cpuMHz']=args.restore_cpu
        check(not owner.state()['recording'] and not owner.state()['recordRequested'],'no_active_recording')
        if args.firmware:
            stage='ota';report['installation']=owner.install(args.firmware)
            check(owner.state('/api/settings/capture')['values']==saved_before,'ota_preserves_all_capture_values')
            for name in ('index.html','app.js','i18n.js','style.css'):
                route='/' if name=='index.html' else '/'+name
                check(owner.request(route,authorized=False)[2]==(ROOT/'ui/appliance'/name).read_bytes(),
                      'deployed_asset_matches_'+name)
        stage='clock_settings'
        code,_,body=owner.request('/api/settings/capture','POST',{'values':{'cpuMHz':80,'powerSave':1,'idleSeconds':30}},timeout=30)
        report['clock_save_code']=code
        if code!=200:report['clock_save_error']=json.loads(body).get('error','unknown')
        check(code==200,
              'lowest_clock_saved')
        check(owner.state()['cpuMHz']==80,'actual_lowest_clock')
        until=time.monotonic()+45
        while time.monotonic()<until and not owner.state()['captureIdle']:time.sleep(.3)
        check(owner.state()['captureIdle'] and not owner.state()['audioInputRunning'],'real_low_clock_camera_and_audio_standby')
        if args.sensor_standby:
            check(owner.state()['cameraSensorSleeping'],'actual_low_clock_sensor_power_down')
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe',args=['--no-proxy-server'])
            page=browser.new_page(viewport=dict(width=390,height=844),reduced_motion='reduce')
            page.on('pageerror',lambda _:report.update(javascript_errors=report['javascript_errors']+1))
            def wait(predicate,seconds):
                # String-based wait_for_function polls through eval and can
                # violate the product CSP while its first predicate is false.
                until=time.monotonic()+seconds
                while time.monotonic()<until:
                    if predicate():return True
                    page.wait_for_timeout(150)
                return False
            stage='anonymous_navigation';page.goto(owner.base+'/',wait_until='domcontentloaded',timeout=20000)
            stage='anonymous_ready'
            check(wait(lambda:'请输入管理员密码' in page.locator('#login-message').inner_text(),15),'anonymous_page_ready')
            stage='language_selection'
            page.locator('#language').select_option('en')
            stage='password_submission'
            page.locator('#password').fill(owner.password);started=time.monotonic();page.locator('#login-button').click()
            check(wait(lambda:page.locator('#password').input_value()=='',10),'password_cleared_during_slow_work')
            check('35–50' in page.locator('#login-message').inner_text() and 'lower clocks' in page.locator('#login-message').inner_text(),
                  'clock_aware_wait_hint')
            probe_at=0
            while time.monotonic()-started<280 and not page.locator('#dashboard').is_visible():
                elapsed=time.monotonic()-started
                if elapsed-probe_at>=30:
                    probe_at=elapsed;at=time.monotonic();state=owner.state()
                    check(state['cpuMHz']==80,'actual_80mhz_during_password_work')
                    report['samples'].append(dict(elapsed_s=round(elapsed,3),status_latency_s=round(time.monotonic()-at,4)))
                    print('LOW-CLOCK-UI waiting seconds',round(elapsed),flush=True)
                page.wait_for_timeout(700)
            report['login_seconds']=time.monotonic()-started
            check(page.locator('#dashboard').is_visible(),'real_browser_login_at_80mhz')
            check(wait(lambda:page.locator('[data-setting]').count()==31,15),'capture_settings_load_at_80mhz')
            check(page.locator('#setting-cpuMHz').input_value()=='80','saved_lowest_clock_displayed')
            check(owner.state()['cpuMHz']==80 and owner.state('/api/wireguard')['peerSessionUp'],'actual_clock_and_wireguard_after_login')
            stage='low_clock_camera_wake'
            before_wake=owner.state()
            with page.expect_response(lambda r:r.url.endswith('/api/camera/still'),timeout=30000) as still:
                page.locator('#still').click()
            check(still.value.status==200,'actual_ui_still_wake_after_low_clock_login')
            check(wait(lambda:page.locator('#preview').evaluate('(x)=>x.naturalWidth===640 && x.naturalHeight===480'),15),
                  'real_still_dimensions_after_low_clock_wake')
            check(owner.state()['cpuMHz']==80 and owner.state()['cameraReady'] and not owner.state()['captureIdle'],
                  'selected_low_clock_restored_after_camera_initialization')
            if args.sensor_standby:
                awake=owner.state()
                check(not awake['cameraSensorSleeping'] and awake['lastWakeFrameMs']>=before_wake['uptimeSeconds']*1000,
                      'actual_ui_sensor_wake_with_fresh_frame')
                check(wait(lambda:page.locator('#photo').is_enabled() and 'standby' not in page.locator('#camera-state').inner_text().casefold(),10),
                      'ui_status_and_controls_follow_actual_wake')
            check(page.evaluate("document.documentElement.scrollWidth<=innerWidth+1"),'no_mobile_overflow')
            check(page.evaluate("sessionStorage.length===0 && Object.keys(localStorage).every(x=>x==='camera-language')"),'language_only_browser_storage')
            page.screenshot(path=str(args.out/'80mhz-mobile.png'),full_page=True)
            page.screenshot(path=str(args.out/'80mhz-mobile-viewport.png'),full_page=False)
            page.locator('#logout').click();page.locator('#login-panel').wait_for(state='visible',timeout=15000)
            check(True,'slow_browser_session_logout');check(report['javascript_errors']==0,'no_javascript_errors')
            browser.close();report['passed']=True
    except Exception as error:
        report.update(stage=stage,error_type=type(error).__name__)
        if stage in ('anonymous_navigation','anonymous_ready','language_selection'):
            # No password has been entered at these stages, so the diagnostic
            # can retain the original browser failure without private inputs.
            report['anonymous_error']=str(error)
        print('LOW-CLOCK-UI failed at',stage,'type',type(error).__name__,flush=True)
    finally:
        try:
            if original is not None:
                restored=False
                addresses=[owner.base]+(['http://'+args.fallback_address] if args.fallback_address else [])
                for base in addresses:
                    owner.base=base
                    try:
                        # This independent session remains RAM-only. An actual
                        # authorized status response establishes the same board
                        # before sending a restore over its alternate address.
                        owner.state()
                        code,_,_=owner.request('/api/settings/capture','POST',{'values':original},timeout=30)
                        restored=code==200 and owner.state()['cpuMHz']==original['cpuMHz']
                    except Exception:continue
                    if restored:
                        report['restored_over_fallback']=base!=addresses[0];break
                check(restored,'original_settings_and_clock_restored')
            owner.request('/api/auth/logout','POST')
        except Exception as error:report.update(passed=False,restore_error=type(error).__name__)
        owner.clear();report['check_count']=len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
