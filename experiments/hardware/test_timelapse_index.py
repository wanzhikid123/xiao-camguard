"""Real JPEG pagination beyond 4096 missing intents, only in owned fixture data."""
import json,time
from pathlib import Path
from appliance_client import Client
ROOT=Path(__file__).resolve().parents[2]
def main():
    c=Client('192.168.1.181',ROOT/'.tools/private/admin-password.txt');owned=None;report={'passed':False,'checks':[]}
    def check(ok,name):
        report['checks'].append({'name':name,'passed':bool(ok)});print('INDEX',bool(ok),name,flush=True)
        if not ok:raise RuntimeError(name)
    try:
        c.login(progress=lambda p,s:print('LOGIN',p,s,flush=True))
        check(c.request('/api/timelapse/start','POST',{'intervalSeconds':5,'immediate':True})[0]==201,'owned_session_created')
        owned=c.state('/api/timelapse')['session'];until=time.monotonic()+30
        while c.state('/api/timelapse')['photos']<2 and time.monotonic()<until:time.sleep(.3)
        c.request('/api/timelapse/stop','POST');until=time.monotonic()+30
        while c.state('/api/timelapse')['active'] and time.monotonic()<until:time.sleep(.3)
        expected={x['name'] for x in c.state('/api/timelapse/files?session='+owned)['items']}
        check(len(expected)>=2,'actual_completed_jpegs_before_padding')
        check(c.request('/api/timelapse/test-index','POST',{'session':owned},timeout=30)[0]==200,'4100_missing_intents_created_only_in_owned_session')
        found=set();cursor='';pages=0
        while True:
            page=c.state('/api/timelapse/files?session='+owned+('&before='+cursor if cursor else ''));pages+=1
            check(not found&{x['name'] for x in page['items']},'page_has_no_duplicates_'+str(pages))
            found.update(x['name'] for x in page['items'])
            if not page['more']:break
            check(page['nextBefore'] and page['nextBefore']!=cursor,'cursor_progresses_even_when_page_is_empty_'+str(pages));cursor=page['nextBefore']
            if pages>100:raise RuntimeError('Index did not make bounded progress')
        check(found==expected and pages>=2,'all_real_photos_reachable_beyond_old_scan_limit');report['passed']=True
    finally:
        if owned:
            c.request('/api/timelapse/stop','POST');time.sleep(2)
            check(c.request('/api/timelapse/delete','POST',{'session':owned},timeout=30)[0]==200,'owned_index_and_session_removed')
        c.clear();(ROOT/'.tools/hardware/timelapse-index-report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if __name__=='__main__':main()
