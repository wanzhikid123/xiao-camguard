"""Real Chrome UI gate at desktop/mobile widths, using the private product account.

No browser trace/HAR/storage dump is created. Error details are sanitized because
Playwright call logs can contain values typed into password controls. Screenshots
and reports stay ignored. Deletion is limited to the photo created by this run.
"""
import argparse
import json
from pathlib import Path
import time
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--password-file',type=Path,default=ROOT/'.tools/private/admin-password.txt')
    parser.add_argument('--chrome',default='C:/Program Files/Google/Chrome/Application/chrome.exe')
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/browser')
    args=parser.parse_args(); args.out.mkdir(parents=True,exist_ok=True)
    password=args.password_file.read_text(encoding='utf-8').rstrip('\r\n')
    report=dict(passed=False,checks=[],credentials_or_tokens_saved=False,viewports=[],javascript_errors=0)
    stage='launch'; owned_photo=''

    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)))
        print(f"BROWSER {'PASS' if ok else 'FAIL'} {name}",flush=True)
        if not ok: raise RuntimeError('UI assertion failed')

    def wait_until(predicate, seconds=12):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            if predicate(): return True
            page.wait_for_timeout(150)
        return False

    def viewport(width,height,label):
        page.set_viewport_size({'width':width,'height':height}); page.wait_for_timeout(350)
        geometry=page.evaluate('({screen:innerWidth,page:document.documentElement.scrollWidth})')
        report['viewports'].append(dict(label=label,**geometry))
        check(geometry['page']<=geometry['screen']+1,'no_horizontal_overflow_'+label)
        page.screenshot(path=str(args.out/(label+'.png')),full_page=True)

    try:
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path=args.chrome,args=['--no-proxy-server'])
            context=browser.new_context(viewport={'width':1366,'height':900},reduced_motion='reduce')
            page=context.new_page()
            page.on('pageerror',lambda _:report.update(javascript_errors=report['javascript_errors']+1))
            stage='login_page'
            page.goto('http://'+args.address+'/',wait_until='domcontentloaded',timeout=15000)
            check(wait_until(lambda:'正在检查' not in page.locator('#login-message').inner_text()),'bootstrap_finishes')
            check(page.title()=='XIAO Camera' and page.locator('#login-panel').is_visible(),'anonymous_login_page')
            check(not page.locator('#dashboard').is_visible(),'anonymous_dashboard_hidden')
            check(page.locator('#login-panel').get_by_label('管理员密码',exact=True).get_attribute('type')=='password','password_control_masked_and_labeled')
            viewport(1366,900,'login-desktop'); viewport(390,844,'login-mobile')
            stage='chosen_password_login'
            page.locator('#login-panel').get_by_label('管理员密码',exact=True).fill(password)
            page.locator('#login-button').click()
            check(wait_until(lambda:page.locator('#password').input_value()==''),'password_field_cleared_during_login')
            check(page.locator('#login-button').is_disabled(),'duplicate_login_disabled')
            check(wait_until(lambda:page.locator('#dashboard').is_visible(),120),'actual_chosen_password_ui_login')
            check(not page.locator('#login-panel').is_visible(),'login_panel_hidden_after_session')
            check(wait_until(lambda:'就绪' in page.locator('#camera-state').inner_text()),'camera_status_updates')
            check(wait_until(lambda:page.locator('#media-list li').count()>0),'authenticated_media_page_loads')
            check(page.evaluate('localStorage.length===0 && sessionStorage.length===0'),'no_auth_in_web_storage')
            viewport(390,844,'dashboard-mobile'); viewport(1366,900,'dashboard-desktop')
            stage='preview'
            page.locator('#preview-toggle').click()
            check(wait_until(lambda:page.locator('#preview').evaluate('(img)=>img.naturalWidth===640 && img.naturalHeight===480')),'live_preview_decodes_in_browser')
            page.screenshot(path=str(args.out/'preview-desktop.png'),full_page=True)
            page.locator('#preview-toggle').click()
            check(not page.locator('#preview').is_visible(),'preview_toggle_closes_display')
            page.locator('#still').click()
            check(wait_until(lambda:page.locator('#preview').is_visible() and page.locator('#preview').evaluate('(img)=>img.naturalWidth===640')),'single_frame_blob_decodes_in_browser')
            stage='photo_and_owned_delete'
            with page.expect_response(lambda response:response.url.endswith('/api/camera/photo') and response.request.method=='POST') as created:
                page.locator('#photo').click()
            check(created.value.status==201,'photo_saved_from_ui')
            owned_photo=created.value.json()['path']; report['owned_photo_path']=owned_photo
            name=owned_photo.rsplit('/',1)[-1]
            check(wait_until(lambda:page.locator('#media-list li').filter(has_text=name).count()==1),'saved_photo_appears_in_media_ui')
            row=page.locator('#media-list li').filter(has_text=name)
            row.get_by_role('button',name='预览',exact=True).click()
            check(wait_until(lambda:page.locator('#preview').evaluate('(img)=>img.naturalWidth===640')),'saved_photo_preview_decodes')
            with page.expect_download() as downloaded:
                row.get_by_role('link',name='下载',exact=True).click()
            downloaded.value.save_as(str(args.out/'ui-photo-download.jpg'))
            check((args.out/'ui-photo-download.jpg').stat().st_size==created.value.json()['bytes'],'browser_download_size_matches_saved_photo')
            page.once('dialog',lambda dialog:dialog.accept())
            with page.expect_response(lambda response:response.url.endswith('/api/media/delete') and response.request.method=='POST') as removed:
                row.get_by_role('button',name='删除',exact=True).click()
            check(removed.value.status==200,'confirmed_ui_deletion_of_own_photo_only')
            check(wait_until(lambda:page.locator('#media-list li').filter(has_text=name).count()==0),'deleted_photo_removed_from_ui')
            stage='manual_audio_recording'
            page.locator('#record-start').click()
            check(wait_until(lambda:page.locator('#record-state').is_visible()),'recording_indicator_visible')
            check(page.locator('#restart').is_disabled(),'restart_disabled_during_recording')
            page.wait_for_timeout(12000)
            page.locator('#record-stop').click()
            check(wait_until(lambda:not page.locator('#record-state').is_visible()),'recording_finalization_finishes_in_ui')
            stage='settings'
            page.get_by_text('Wi-Fi 设置',exact=True).click()
            page.get_by_text('固件更新',exact=True).click()
            check(page.get_by_label('新的 2.4 GHz Wi-Fi 名称',exact=True).is_visible(),'wifi_settings_labels_and_expansion')
            check(page.get_by_label('SHA-256 校验值',exact=True).is_visible(),'ota_settings_labels_and_expansion')
            check(page.locator('#ota-submit').is_enabled(),'ota_available_after_record_finalization')
            viewport(390,844,'settings-mobile'); viewport(768,1024,'settings-tablet'); viewport(1366,900,'settings-desktop')
            stage='session_refresh_and_logout'
            page.reload(wait_until='domcontentloaded')
            check(wait_until(lambda:page.locator('#dashboard').is_visible()),'refresh_restores_session_without_password')
            page.locator('#logout').click()
            check(wait_until(lambda:page.locator('#login-panel').is_visible()),'ui_logout_returns_to_login')
            check(not page.locator('#dashboard').is_visible() and page.locator('#media-list li').count()==0,'logout_clears_private_dashboard')
            check(page.evaluate('localStorage.length===0 && sessionStorage.length===0'),'web_storage_empty_after_logout')
            check(report['javascript_errors']==0,'no_uncaught_javascript_errors')
            context.close(); browser.close(); report['passed']=True
    except Exception as error:
        # Never include Playwright call logs: fill() may echo the private input.
        report['error']=type(error).__name__; report['failed_stage']=stage
        print('Browser failure:',stage,type(error).__name__,flush=True)
    finally:
        password=''; report['check_count']=len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
