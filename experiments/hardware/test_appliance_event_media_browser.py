"""Normal image acceptance; bulk UI inventories are limited to owned test files.

Actual batch deletions always pass an ownership guard. Original SD files are
never sent to a deletion endpoint. Synthetic pagination scans are intercepted
and make no SD changes. Browser session/private values remain in RAM.
"""
import argparse
import json
from pathlib import Path
import time
from urllib.parse import parse_qs, urlparse, quote
from playwright.sync_api import sync_playwright, expect
from appliance_client import Client
from full_mode_tools import ROOT, inventory, decode_avi, expected_exit


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--address',required=True)
    parser.add_argument('--firmware',type=Path);parser.add_argument('--out',type=Path,default=ROOT/'.tools/hardware/event-media-browser')
    parser.add_argument('--cleanup-owned-report',type=Path,help='Remove only leftover test files recorded by this previous private gate report')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    c=Client(args.address,ROOT/'.tools/private/admin-password.txt');owned=set();old=None
    report=dict(passed=False,checks=[],javascript_errors=0,credentials_or_tokens_saved=False,originals_never_deleted=True)
    def check(ok,name):
        report['checks'].append(dict(name=name,passed=bool(ok)));print('EVENT-MEDIA','PASS' if ok else 'FAIL',name,flush=True)
        if not ok:raise RuntimeError('Event/media check: '+name)
    def wait(predicate,seconds=15):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            state=c.state()
            if predicate(state):return state
            time.sleep(.2)
        raise RuntimeError('State deadline')
    def batch(paths,**kwargs):
        if not set(paths)<=owned:raise RuntimeError('Deletion ownership guard')
        return c.request('/api/media/delete-batch','POST',{'paths':paths},**kwargs)
    try:
        c.login(progress=lambda phase,seconds:print('LOGIN',phase,seconds,flush=True));expected=expected_exit(c)
        if args.firmware:report['installation']=c.install(args.firmware)
        if args.cleanup_owned_report:
            prior=json.loads(args.cleanup_owned_report.read_text(encoding='utf8'))
            cleanup=prior['remaining_owned_test_files']
            if prior.get('originals_never_deleted') is not True or len(set(cleanup))!=len(cleanup):raise RuntimeError('Invalid ownership report')
            present=inventory(c);cleanup=[p for p in cleanup if p in present]
            for i in range(0,len(cleanup),20):
                code,_,body=c.request('/api/media/delete-batch','POST',raw=json.dumps({'paths':cleanup[i:i+20]},ensure_ascii=False).encode(),headers={'Content-Type':'application/json'})
                check(code==200 and json.loads(body)['deleted']==len(cleanup[i:i+20]),'prior_owned_cleanup_'+str(i))
        old=inventory(c);values=c.state('/api/settings/capture')['values']
        for filename,uri in (('index.html','/'),('app.js','/app.js'),('i18n.js','/i18n.js'),('style.css','/style.css')):
            code,_,asset=c.request(uri,authorized=False)
            check(code==200 and asset==(ROOT/'ui/appliance'/filename).read_text(encoding='utf-8').encode(),'normal_asset_matches_source_'+filename)
        check(c.request('/api/events/test')[0]==404,'no_test_route_in_normal_image')
        check(not c.state()['events']['armed'],'normal_boot_disarmed')
        for i in range(23):
            code,_,body=c.request('/api/camera/photo','POST',timeout=30)
            check(code==201,'owned_photo_'+str(i));owned.add(json.loads(body)['path'])
        photo=sorted(owned)[0]
        check(batch([photo],authorized=False)[0]==403,'batch_requires_login')
        check(batch([photo],headers={'X-CSRF-Token':''})[0]==403,'batch_requires_csrf')
        check(batch([photo],headers={'Origin':'http://example.invalid'})[0]==403,'batch_requires_origin')
        for body in ({'paths':[]},{'paths':[photo,photo]},{'paths':[photo]*21},{'paths':[photo],'extra':1},{'paths':[photo,{}]},{'paths':[photo,'/config.txt']}):
            check(c.request('/api/media/delete-batch','POST',body)[0]==400,'invalid_batch_'+str(len(report['checks'])))
        missing=photo[:-4]+'_not_present.jpg'
        check(c.request('/api/media/delete-batch','POST',{'paths':[photo,missing]})[0]==404,'missing_member_aborts_entire_batch')
        check(photo in inventory(c),'valid_first_member_retained_after_invalid_batch')
        before=inventory(c);check(c.request('/api/record/start','POST')[0]==202,'manual_record_start')
        wait(lambda s:s['recording']);check(batch([photo])[0]==409,'batch_blocked_during_recording');time.sleep(6)
        check(c.request('/api/record/stop','POST')[0]==202,'manual_record_stop');wait(lambda s:not s['recording'])
        clips=[p for p in inventory(c) if p not in before and p.endswith('.avi')]
        check(len(clips)==1,'one_owned_manual_regression_clip');owned.add(clips[0])
        target=args.out/'manual.avi';c.download('/api/media/download?path='+quote(clips[0],safe=''),target)
        report['manual']=decode_avi(target);check(report['manual']['passed'] and report['manual']['whole_decode_passed'],'manual_mjpeg_pcm_whole_decode')
        normal_width=c.state()['videoWidth']
        check(c.request('/api/events/start','POST',{'sensitivity':2})[0]==202,'normal_real_detector_arm')
        state=wait(lambda s:s['events']['armed']);time.sleep(4)
        state=c.state();check(state['events']['error']=='none' and state['videoWidth']==640 and state['videoHeight']==480,'real_jpeg_detector_runs_without_errors')
        check(batch([photo])[0]==409,'batch_blocked_during_monitoring')
        check(c.request('/api/events/stop','POST')[0]==202,'normal_disarm')
        state=wait(lambda s:not s['events']['armed'] and not s['recording'])
        check(state['videoWidth']==normal_width,'disarmed_state_reports_restored_video_resolution')
        code,_,body=c.request('/api/media/rename','POST',raw=json.dumps({'path':photo,'name':'抓拍删除测试'},ensure_ascii=False).encode(),headers={'Content-Type':'application/json'})
        check(code==200,'owned_unicode_filename_prepared_for_bulk_delete')
        owned.discard(photo);owned.add(json.loads(body)['path'])
        owned_size=inventory(c);original_owned=set(owned);batch_calls=[];virtual=[];scan_mode='owned'
        def scoped_list(route):
            query=parse_qs(urlparse(route.request.url).query);cursor=query.get('before',[''])[0]
            if scan_mode=='truncated':
                route.fulfill(json=dict(items=[],more=False,nextBefore='',enumerationTruncated=True));return
            if scan_mode=='virtual':
                source=virtual
            else:source=[dict(path=p,bytes=owned_size[p],kind='photo' if p.endswith('.jpg') else 'video',name=p.rsplit('/',1)[1]) for p in owned]
            if urlparse(route.request.url).path.endswith('/dates'):
                entries=[dict(date=d) for d in sorted({x['path'][1:9] for x in source},reverse=True)]
                key='date'
            else:
                date=query.get('date',[''])[0];entries=[x for x in source if x['path'][1:9]==date];key='name'
                entries.sort(key=lambda x:x[key],reverse=True)
            entries=[x for x in entries if not cursor or x[key]<cursor];page=entries[:20]
            route.fulfill(json=dict(items=page,more=len(entries)>20,nextBefore=page[-1][key] if len(entries)>20 else '',enumerationTruncated=False))
        def guarded_delete(route):
            paths=route.request.post_data_json['paths']
            if scan_mode=='virtual':
                if not set(paths)<={x['path'] for x in virtual}:raise RuntimeError('Virtual ownership guard')
                batch_calls.append(list(paths));route.fulfill(json=dict(deleted=len(paths),error='none'));return
            if not set(paths)<=owned or any(p in old for p in paths):raise RuntimeError('Original media deletion blocked by test guard')
            response=route.fetch();result=response.json();batch_calls.append(list(paths))
            for path in paths[:result.get('deleted',0)]:owned.discard(path)
            route.fulfill(response=response)
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe',args=['--no-proxy-server'])
            context=browser.new_context(viewport=dict(width=390,height=844),reduced_motion='reduce')
            context.add_cookies([dict(name='cam_session',value=c.cookie.split('=',1)[1],url=c.base,httpOnly=True,sameSite='Strict')])
            page=context.new_page();page.on('pageerror',lambda _:report.update(javascript_errors=report['javascript_errors']+1))
            page.route('**/api/media/dates*',scoped_list);page.route('**/api/media?*',scoped_list)
            page.route('**/api/media/delete-batch',guarded_delete)
            page.goto(c.base+'/',wait_until='domcontentloaded');page.locator('#dashboard').wait_for(state='visible',timeout=15000)
            expect(page.locator('[data-media-choice]')).to_have_count(20,timeout=15000)
            for lang,label in (('cn','删除所选'),('en','Delete selected'),('de','Auswahl löschen')):
                page.locator('#language').select_option(lang);page.wait_for_timeout(600)
                check(page.locator('#media-delete-selected').inner_text()==label,'localized_bulk_controls_'+lang)
                for width in (390,768,1366):
                    page.set_viewport_size(dict(width=width,height=900));page.wait_for_timeout(200)
                    check(page.evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'no_overflow_'+lang+'_'+str(width))
                page.set_viewport_size(dict(width=390,height=900));page.locator('.media-card').scroll_into_view_if_needed()
                page.screenshot(path=str(args.out/('media-'+lang+'.png')),animations='disabled')
            page.locator('#language').select_option('en');page.wait_for_timeout(500)
            page.locator('#event-sensitivity').select_option('2');page.locator('#event-start').click()
            wait(lambda s:s['events']['armed']);page.locator('#event-stop').wait_for(state='visible',timeout=10000)
            check(page.locator('#media-delete-all').is_disabled(),'browser_disables_clear_while_armed')
            page.locator('#event-stop').click();wait(lambda s:not s['events']['armed'] and not s['recording'])
            page.wait_for_timeout(3000);check(page.locator('#event-start').is_visible(),'browser_event_start_stop_actual_api')
            page.locator('[data-media-choice]').first.check();page.locator('#media-more').click()
            expect(page.locator('[data-media-choice]')).to_have_count(24,timeout=15000)
            page.locator('[data-media-choice]').last.check()
            check('2 files selected' in page.locator('#media-selection-count').inner_text(),'selection_spans_loaded_pages')
            page.once('dialog',lambda dialog:dialog.dismiss());page.locator('#media-delete-selected').click();page.wait_for_timeout(600)
            check(len(owned)==24 and not batch_calls,'cancel_does_not_delete_files')
            page.once('dialog',lambda dialog:dialog.accept());page.locator('#media-delete-selected').click()
            expect(page.locator('#media-message')).to_have_text('Deleted 2 files.',timeout=15000)
            check(len(owned)==22 and len(batch_calls)==1,'selected_two_actual_owned_files_deleted')
            page.locator('#media-select-all').check();check(page.locator('[data-media-choice]:checked').count()==20,'select_all_applies_to_loaded_rows')
            page.locator('#media-select-all').uncheck();check(page.locator('[data-media-choice]:checked').count()==0,'select_all_can_clear_selection')
            # Delete-all must enumerate the second page despite only 20 visible rows.
            page.once('dialog',lambda dialog:dialog.accept());page.locator('#media-delete-all').click()
            expect(page.locator('#media-message')).to_have_text('Deleted 22 files.',timeout=20000)
            check(not owned and [len(x) for x in batch_calls]==[2,20,2],'all_owned_files_deleted_in_bounded_batches')
            check(page.locator('[data-media-choice]').count()==0,'empty_gallery_after_clear')
            # 21 dates and >20 files in one date: exercise both independent cursors.
            scan_mode='virtual';batch_calls.clear()
            for day in range(1,22):
                date=f'209901{day:02d}'
                for i in range(21 if day==1 else 1):
                    name=f'{date}_120000_{i:06d}_photo.jpg'
                    virtual.append(dict(path=f'/{date}/{name}',name=name,bytes=1234,kind='photo'))
            page.locator('#media-refresh').click();page.wait_for_timeout(500)
            page.once('dialog',lambda dialog:dialog.accept());page.locator('#media-delete-all').click()
            expect(page.locator('#media-message')).to_have_text('Deleted 41 files.',timeout=20000)
            check({p for call in batch_calls for p in call}=={x['path'] for x in virtual} and [len(x) for x in batch_calls]==[20,20,1],'all_dates_and_file_pages_enumerated_before_clear')
            scan_mode='truncated';batch_calls.clear();page.locator('#media-delete-all').click();page.wait_for_timeout(800)
            check(not batch_calls and 'incomplete scan' in page.locator('#media-message').inner_text(),'incomplete_scan_never_starts_deletion')
            check(report['javascript_errors']==0,'no_browser_javascript_errors')
            check(page.evaluate("sessionStorage.length===0&&Object.keys(localStorage).every(x=>x==='camera-language')"),'no_private_browser_storage')
            browser.close()
        media=inventory(c);check(all(media.get(path)==size for path,size in old.items()),'all_original_media_retained')
        check(not (original_owned&set(media)),'all_owned_test_deletions_verified_by_real_inventory')
        check(c.state('/api/settings/capture')['values']==values,'all_stored_camera_settings_retained')
        state=c.state();wg=c.state('/api/wireguard')
        check(state['cameraReady'] and state['sdReady'] and not state['events']['armed'] and wg['fullTunnelVerified'] and wg['publicExit'] in expected,'final_camera_sd_disarmed_full_tunnel_healthy')
        report.update(passed=True,original_media_count=len(old),actual_owned_deleted=24,virtual_scan_files=41)
    except Exception as failure:
        report['error_type']=type(failure).__name__;print('EVENT-MEDIA FAIL',str(failure),flush=True)
    finally:
        try:
            c.request('/api/events/stop','POST');c.request('/api/record/stop','POST')
        except Exception:pass
        report['remaining_owned_test_files']=sorted(owned)
        (args.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');c.clear()
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
