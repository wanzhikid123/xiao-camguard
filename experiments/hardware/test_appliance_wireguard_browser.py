"""Real Chrome WG-A field/import/restart/clear/cancel gate; no private dumps."""
import argparse
import json
from pathlib import Path
import re
import time
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--password-file', type=Path, default=ROOT/'.tools/private/admin-password.txt')
    parser.add_argument('--config', type=Path, default=ROOT/'XIAO_ESP32_Camera.conf')
    parser.add_argument('--out', type=Path, default=ROOT/'.tools/hardware/browser-wireguard')
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=True)
    password = args.password_file.read_text(encoding='utf-8').rstrip('\r\n')
    raw = args.config.read_text(encoding='utf-8-sig')
    fields = {key.lower(): value.strip() for key, value in re.findall(r'^\s*(\w+)\s*=\s*([^\r\n]*)', raw, re.M)}
    host, _, port = fields['endpoint'].rpartition(':')
    fields['address'] = next(item.strip() for item in fields['address'].split(',') if ':' not in item)
    fields['allowedips'] = ','.join(item.strip() for item in fields['allowedips'].split(',') if ':' not in item)
    secrets = [password.encode()]+[fields[key].encode() for key in ('privatekey','publickey','presharedkey') if fields.get(key)]
    report = dict(passed=False, checks=[], javascript_errors=0, credentials_or_tokens_saved=False, viewports=[])
    stage = 'launch'

    def check(ok, name):
        report['checks'].append(dict(name=name, passed=bool(ok)))
        print(f"WG-UI {'PASS' if ok else 'FAIL'} {name}", flush=True)
        if not ok: raise RuntimeError('WG UI assertion failed')

    def state(path='/api/wireguard'):
        response = page.request.get('http://'+args.address+path)
        body = response.body()
        if any(secret in body for secret in secrets): raise RuntimeError('Private material in response')
        if response.status != 200: raise RuntimeError('WG UI state unavailable')
        return response.json()

    def wait(predicate, seconds=25):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            if predicate(): return True
            page.wait_for_timeout(200)
        return False

    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True,
                executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe', args=['--no-proxy-server'])
            context = browser.new_context(viewport={'width':1366,'height':900}, reduced_motion='reduce')
            page = context.new_page(); page.on('pageerror', lambda _: report.update(javascript_errors=report['javascript_errors']+1))
            page.goto('http://'+args.address+'/', wait_until='domcontentloaded')
            check(wait(lambda: '正在检查' not in page.locator('#login-message').inner_text()), 'bootstrap_finishes')
            stage = 'private_login'; page.locator('#password').fill(password); page.locator('#login-button').click()
            check(wait(lambda: page.locator('#dashboard').is_visible(), 120), 'chosen_account_ui_login')
            old_revision = state('/api/status')['configRevision']
            page.get_by_text('WireGuard 远程访问', exact=True).click()
            check(wait(lambda: '会话已建立' in page.locator('#wg-message').inner_text()), 'actual_peer_state_displayed')
            stage = 'private_file_import'
            page.locator('#wg-file').set_input_files(str(args.config))
            check(wait(lambda: page.locator('#wg-config').input_value().lstrip('\ufeff') == raw.lstrip('\ufeff')), 'private_file_loaded_locally')
            with page.expect_response(lambda response: response.url.endswith('/api/wireguard/import') and response.request.method == 'POST') as file_imported:
                page.locator('#wg-submit').click()
            check(file_imported.value.status == 202, 'file_import_accepted')
            check(page.locator('#wg-config').input_value() == '' and page.locator('#wg-file').input_value() == '', 'file_and_raw_private_input_cleared')
            check(wait(lambda: state('/api/network')['state'] == 'awaiting_local_confirmation'), 'file_import_actual_peer_trial_ready')
            page.get_by_text('Wi-Fi 设置', exact=True).click()
            check(wait(lambda: page.locator('#network-cancel').is_visible()), 'file_trial_cancel_available')
            page.locator('#network-cancel').click()
            check(wait(lambda: state('/api/network')['state'] == 'ready' and state()['peerSessionUp']), 'file_trial_cancel_restores_active_peer')
            page.get_by_text('逐项填写配置', exact=True).click(); stage = 'private_fields'
            for key in ('private','public','preshared'):
                check(page.locator('#wg-'+key+'-key').get_attribute('type') == 'password', key+'_key_input_masked')
            values = {'address':fields['address'], 'private-key':fields['privatekey'], 'public-key':fields['publickey'],
                      'preshared-key':fields.get('presharedkey',''), 'endpoint':host, 'port':port,
                      'allowed-ips':fields['allowedips'], 'dns':fields.get('dns',''),
                      'mtu':fields.get('mtu','1420'), 'keepalive':fields.get('persistentkeepalive','0')}
            for name, value in values.items(): page.locator('#wg-'+name).fill(value)
            with page.expect_response(lambda response: response.url.endswith('/api/wireguard/import') and response.request.method == 'POST') as imported:
                page.locator('#wg-fields-submit').click()
            values = {}; check(imported.value.status == 202, 'individual_fields_import_accepted')
            check(all(page.locator('#wg-'+key+'-key').input_value() == '' for key in ('private','public','preshared')), 'all_field_keys_cleared_after_submit')
            check(wait(lambda: state('/api/network')['state'] == 'awaiting_local_confirmation'), 'field_config_actual_session_trial_ready')
            check(wait(lambda: page.locator('#network-confirm').is_visible()), 'local_confirmation_button_available')
            page.locator('#network-confirm').click()
            check(wait(lambda: state('/api/network')['state'] == 'ready' and not state('/api/network')['revision']), 'field_trial_confirmed_via_actual_lan_ui')
            check(state('/api/status')['configRevision'] > old_revision and state()['peerSessionUp'], 'field_config_promoted_and_peer_active')
            stage = 'restart'
            with page.expect_response(lambda response: response.url.endswith('/api/wireguard/restart')) as restarted:
                page.locator('#wg-restart').click()
            check(restarted.value.status == 202, 'ui_restart_accepted')
            page.wait_for_timeout(1500)
            check(wait(lambda: state('/api/network')['state'] == 'ready' and state()['peerSessionUp']), 'ui_restart_restores_authenticated_peer')
            revision = state('/api/status')['configRevision']; check(revision > old_revision, 'restart_keeps_selected_config')
            stage = 'clear_cancel'
            page.once('dialog', lambda dialog: dialog.accept())
            with page.expect_response(lambda response: response.url.endswith('/api/wireguard/clear')) as cleared:
                page.locator('#wg-clear').click()
            check(cleared.value.status == 202, 'confirmed_ui_clear_starts_trial')
            check(wait(lambda: state('/api/network')['state'] == 'awaiting_local_confirmation' and not state()['running']), 'clear_trial_stops_interface')
            check(state('/api/status')['configRevision'] == revision, 'clear_trial_preserves_active_until_confirmation')
            check(wait(lambda: page.locator('#network-cancel').is_visible()), 'ui_cancel_available')
            page.locator('#network-cancel').click()
            check(wait(lambda: state('/api/network')['state'] == 'ready' and state()['peerSessionUp']), 'cancel_clear_restores_keys_and_session')
            stage = 'layouts'
            # Only after private controls are empty; never screenshot raw config.
            check(page.locator('#wg-config').input_value() == '', 'raw_import_control_empty_before_screenshots')
            for width, height in ((390,844),(768,1024),(1366,900)):
                page.set_viewport_size({'width':width, 'height':height}); page.wait_for_timeout(250)
                geometry = page.evaluate('({screen:innerWidth,page:document.documentElement.scrollWidth})')
                report['viewports'].append(geometry)
                check(geometry['page'] <= width+1, f'expanded_wg_fields_no_horizontal_overflow_{width}')
                page.screenshot(path=str(args.out/f'wg-fields-{width}.png'), full_page=True)
            stage = 'logout'
            page.locator('#logout').click(); check(wait(lambda: page.locator('#login-panel').is_visible()), 'wg_ui_logout')
            check(page.locator('#wg-config').input_value() == '' and all(page.locator('#wg-'+key+'-key').input_value() == '' for key in ('private','public','preshared')), 'wg_private_inputs_empty_after_logout')
            check(page.evaluate('localStorage.length === 0 && sessionStorage.length === 0'), 'no_private_web_storage')
            check(report['javascript_errors'] == 0, 'no_uncaught_javascript_errors')
            context.close(); browser.close(); report['passed'] = True
    except Exception as error:
        report['failed_stage'] = stage; report['error'] = type(error).__name__
        print('WG browser failure:', stage, type(error).__name__, flush=True)
    finally:
        password = raw = ''; fields = {}; secrets = []; report['check_count'] = len(report['checks'])
        (args.out/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
