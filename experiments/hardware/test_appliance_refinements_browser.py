"""Actual Chrome gate for light UI, all three languages and capture settings.

Private password is used only in RAM and cleared before screenshots. Exception
details are omitted because browser automation logs can echo typed values.
"""
import argparse
import json
from pathlib import Path
import time
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/browser-refinements')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    password=(ROOT/'.tools/private/admin-password.txt').read_text(encoding='utf8').rstrip('\r\n')
    report=dict(passed=False,checks=[],javascript_errors=0,viewports=[],credentials_or_tokens_saved=False)
    stage='launch';original=None
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)));print('REFINE-UI','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('UI assertion')
    def wait(predicate,seconds=15):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if predicate():return True
            page.wait_for_timeout(150)
        return False
    try:
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe',args=['--no-proxy-server'])
            context=browser.new_context(viewport=dict(width=1366,height=900),reduced_motion='reduce');page=context.new_page()
            page.on('pageerror',lambda _:report.update(javascript_errors=report['javascript_errors']+1))
            stage='login';page.goto('http://'+args.address+'/',wait_until='domcontentloaded',timeout=20000)
            check(wait(lambda:page.locator('#login-panel').is_visible() and '正在检查' not in page.locator('#login-message').inner_text()),'anonymous_page_ready')
            check(page.evaluate("getComputedStyle(document.documentElement).colorScheme==='light'"),'light_system_theme')
            for lang,expected in [('en','Administrator password'),('de','Administratorpasswort'),('cn','管理员密码')]:
                page.locator('#language').select_option(lang);check(page.locator('#login-panel').get_by_label(expected,exact=True).count()==1,'translated_login_'+lang)
            page.locator('#password').fill(password);page.locator('#login-button').click()
            check(wait(lambda:page.locator('#password').input_value()==''),'password_cleared_before_verification')
            check(wait(lambda:'240 MHz' in page.locator('#login-message').inner_text() and
                       '35–50' in page.locator('#login-message').inner_text()),'clock_aware_login_wait_hint')
            check(wait(lambda:page.locator('#dashboard').is_visible(),150),'chosen_account_ui_login_over_wg')
            check(wait(lambda:page.locator('[data-setting]').count()==31),'all_capture_controls_loaded')
            original=page.evaluate("Object.fromEntries([...document.querySelectorAll('[data-setting]')].map(x=>[x.dataset.setting,Number(x.value)]))")
            check('dBm' in page.locator('#network').inner_text() and any(x in page.locator('#network').inner_text() for x in ['优秀','良好','一般','较差','很差']),'rssi_and_signal_description')
            check(wait(lambda:'%' in page.locator('#cpu-usage').inner_text() and '%' in page.locator('#ram-usage').inner_text()),'cpu_ram_percentages_displayed')
            check(page.locator('#setting-photoResolution option').filter(has_text='2048 × 1536').count()==1,'3mp_photo_option')
            check(page.locator('#setting-cpuMHz option').count()==3,'three_supported_cpu_frequencies')
            check(page.locator('#setting-micGain option').filter(has_text='4× (+12 dB)').count()==1,'audio_gain_multiplier_and_db')
            stage='languages_and_layout';page.locator('#capture-settings').evaluate('(x)=>x.open=true')
            for lang,title,label in [('cn','照片与录像','录像分辨率'),('en','Photos and videos','Video resolution'),('de','Fotos und Videos','Videoauflösung')]:
                page.locator('#language').select_option(lang);page.wait_for_timeout(700)
                check(page.locator('#media-title').inner_text()==title,'media_title_'+lang)
                check(page.locator('label[for="setting-videoResolution"]').inner_text()==label,'capture_label_'+lang)
                check(page.locator('#setting-micGain option[value="0"]').text_content()=={
                    'cn':'关闭（仅预览与拍照）','en':'Off (preview and photos only)',
                    'de':'Aus (nur Vorschau und Fotos)'}[lang],'mic_off_scope_translated_'+lang)
                check(page.locator('#cpu-meter').get_attribute('aria-label')=={'cn':'CPU 占用率','en':'CPU usage','de':'CPU-Auslastung'}[lang],'metric_accessibility_'+lang)
                for width in (390,768,1366):
                    page.set_viewport_size(dict(width=width,height=900));page.wait_for_timeout(150)
                    geometry=page.evaluate('({screen:innerWidth,page:document.documentElement.scrollWidth})')
                    report['viewports'].append(dict(language=lang,width=width,**geometry));check(geometry['page']<=width+1,'no_overflow_'+lang+'_'+str(width))
                    page.screenshot(path=str(args.out/(lang+'-'+str(width)+'.png')),full_page=True)
            page.reload(wait_until='domcontentloaded');check(wait(lambda:page.locator('[data-setting]').count()==31),'settings_load_on_saved_language_reload')
            page.locator('#capture-settings').evaluate('(x)=>x.open=true')
            check(page.locator('label[for="setting-videoResolution"]').inner_text()=='Videoauflösung','german_preference_persists')
            page.locator('#language').select_option('en');check(page.locator('label[for="setting-videoResolution"]').inner_text()=='Video resolution','switch_from_initial_german_to_english')
            check(page.locator('#setting-powerSave option').all_text_contents()==['On','Off'],'toggle_translation_after_saved_german_reload')
            check(page.evaluate("sessionStorage.length===0 && Object.keys(localStorage).every(x=>x==='camera-language')"),'only_language_code_in_web_storage')
            stage='settings_save';page.locator('#capture-settings').evaluate('(x)=>x.open=true')
            page.locator('#setting-contrast').fill(str(1 if original['contrast']!=1 else 0))
            with page.expect_response(lambda r:r.url.endswith('/api/settings/capture') and r.request.method=='POST',timeout=30000) as saved:
                page.locator('#capture-submit').click()
            check(saved.value.status==200,'actual_ui_parameter_save')
            check(wait(lambda:'Settings saved' in page.locator('#capture-message').inner_text()),'translated_save_result')
            page.locator('#setting-contrast').fill(str(original['contrast']))
            with page.expect_response(lambda r:r.url.endswith('/api/settings/capture') and r.request.method=='POST',timeout=30000) as restored:
                page.locator('#capture-submit').click()
            check(restored.value.status==200,'original_contrast_restored_from_ui')
            stage='photo_rename';page.locator('#language').select_option('en')
            with page.expect_response(lambda r:r.url.endswith('/api/camera/photo') and r.request.method=='POST',timeout=30000) as photo:
                page.locator('#photo').click()
            check(photo.value.status==201,'photo_saved_from_refined_ui');path=photo.value.json()['path'];name=path.rsplit('/',1)[-1]
            check(wait(lambda:page.locator('#media-list li').filter(has_text=name).count()==1),'owned_photo_row_loaded')
            row=page.locator('#media-list li').filter(has_text=name);page.once('dialog',lambda d:d.accept('UI Garten 01'))
            with page.expect_response(lambda r:r.url.endswith('/api/media/rename') and r.request.method=='POST') as renamed:
                row.get_by_role('button',name='Rename',exact=True).click()
            check(renamed.value.status==200,'actual_ui_file_rename');new_name=renamed.value.json()['path'].rsplit('/',1)[-1]
            check(wait(lambda:page.locator('#media-list li').filter(has_text=new_name).count()==1),'renamed_row_refreshes')
            page.locator('#media-list li').filter(has_text=new_name).get_by_role('button',name='Preview',exact=True).click()
            check(wait(lambda:page.locator('#preview').evaluate('(x)=>x.naturalWidth===2048 && x.naturalHeight===1536')),'renamed_3mp_photo_previews')
            page.locator('#language').select_option('cn');page.locator('#logout').click()
            check(wait(lambda:page.locator('#login-panel').is_visible()),'ui_logout')
            check(report['javascript_errors']==0,'no_javascript_errors');report['passed']=True;browser.close()
    except Exception as error:
        report.update(stage=stage,error_type=type(error).__name__);print('REFINE-UI failed at',stage,'type',type(error).__name__,flush=True)
    finally:
        if original is not None and not report['passed']:
            from appliance_client import Client
            client=Client(args.address,ROOT/'.tools/private/admin-password.txt')
            try:
                client.login()
                if client.state('/api/settings/capture')['values']!=original:
                    code,_,_=client.request('/api/settings/capture','POST',{'values':original},timeout=30)
                    report['failure_settings_restored']=code==200
                client.request('/api/auth/logout','POST')
            except Exception:
                report['restore_error']='Capture settings require follow-up'
            finally:client.clear()
        password='';report['check_count']=len(report['checks']);(args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
