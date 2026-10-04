"""Real Windows BLE checks. PIN and credentials remain only in RAM."""
import asyncio
import time
from bleak import BleakClient,BleakScanner
from winrt.windows.devices.enumeration import (DeviceInformation,DevicePairingKinds,
    DevicePairingProtectionLevel,DevicePairingResultStatus)
from winrt.windows.devices.bluetooth import BluetoothLEDevice

def uuid(number):return f'7a1000{number:02x}-8c3a-4b9f-a7b1-5d23e1000000'

async def checks(http,check,owned_sessions,created_bond,wifi=False):
    owned_session=None
    device=await BleakScanner.find_device_by_filter(lambda device,advert:advert.local_name=='XIAO-Camera-4C08',timeout=15)
    check(device is not None,'ble_advertised_name_and_discovery')
    async with BleakClient(device,winrt={'use_cached_services':False}) as client:
        status=await client.read_gatt_char(uuid(1))
        check(bool(status),'ble_public_status_without_pairing')
        secret=client.services.get_characteristic(uuid(4))
        check('read' not in secret.properties and 'notify' not in secret.properties,'ble_password_write_only')
        info=await DeviceInformation.create_from_id_async(client._backend._requester.device_information.id)
        was_paired=info.pairing.is_paired
        created_bond[0]=not was_paired
        try:
            await authenticated_checks(http,check,owned_sessions,client,info,was_paired,wifi)
        finally:
            if not was_paired:await client.unpair()

async def authenticated_checks(http,check,owned_sessions,client,info,was_paired,wifi):
        if not was_paired:
            # WinRT custom pairing avoids displaying a system dialog. The PIN
            # comes from the authenticated Web endpoint and is never logged.
            pin=http.state('/api/settings/ble')['pin']
            custom=info.pairing.custom
            def requested(sender,args):
                if args.pairing_kind==DevicePairingKinds.PROVIDE_PIN:args.accept_with_pin(pin)
            token=custom.add_pairing_requested(requested)
            try:
                paired=await custom.pair_with_protection_level_async(DevicePairingKinds.PROVIDE_PIN,
                    DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION)
                check(paired.status in (DevicePairingResultStatus.PAIRED,DevicePairingResultStatus.ALREADY_PAIRED),'ble_pin_authenticated_pairing')
            finally:custom.remove_pairing_requested(token);pin=''
        else:check(True,'ble_existing_windows_bond_retained')
        notices=[]
        await client.start_notify(uuid(20),lambda _,value:notices.append(bytes(value)))
        # A harmless staging write verifies encryption/authentication and the
        # worker mailbox. No APPLY is sent and no network configuration changes.
        await client.write_gatt_char(uuid(3),b'ESP32-acceptance-staged-only',response=True)
        await asyncio.sleep(.3)
        check(http.state('/api/status')['ble']['authenticated'],'ble_connection_authenticated_and_bonded')
        check(any(x==b'ACCEPTED' for x in notices),'ble_worker_result_notification')
        await client.write_gatt_char(uuid(18),b'5',response=True)
        await client.write_gatt_char(uuid(19),b'START',response=True)
        await asyncio.sleep(1)
        state=http.state('/api/timelapse')
        check(state['active'],'ble_starts_interval_photo_owner')
        owned_session=state['session'];owned_sessions.add(owned_session)
        await client.write_gatt_char(uuid(19),b'STOP',response=True)
        for _ in range(40):
            if not http.state('/api/timelapse')['active']:break
            await asyncio.sleep(.5)
        check(not http.state('/api/timelapse')['active'],'ble_stops_interval_photo_owner')
        check(http.request('/api/timelapse/delete','POST',{'session':owned_session})[0]==200,'ble_owned_session_cleanup')
        if wifi:
            from pathlib import Path
            from full_mode_tools import inventory
            before=http.state('/api/network');wg=http.state('/api/wireguard');media=inventory(http)
            ssid=bytes(await client.read_gatt_char(uuid(3))).decode()
            # First prove the failed target rolls back without committing NVS.
            await client.write_gatt_char(uuid(3),b'ESP32-acceptance-no-such-network',response=True)
            await asyncio.sleep(.2);await client.write_gatt_char(uuid(4),b'not-a-real-network-password',response=True)
            await asyncio.sleep(.2);await client.write_gatt_char(uuid(5),b'APPLY',response=True)
            await asyncio.sleep(5)
            until=time.monotonic()+100;recovered=False
            while time.monotonic()<until:
                try:
                    state=http.state('/api/network')
                    if state['revision']==0 and state['wifiConnected'] and state['error']=='connect_timeout':recovered=True;break
                except (OSError,TimeoutError,RuntimeError):pass
                await asyncio.sleep(1)
            check(recovered,'ble_failed_wifi_trial_rolls_back')
            check(state['activeRevision']==before['activeRevision'],'ble_failed_wifi_not_saved')
            password=(Path(__file__).resolve().parents[2]/'.tools/private/wifi-password.txt').read_text(encoding='utf8').rstrip('\r\n')
            await client.write_gatt_char(uuid(3),ssid.encode(),response=True);await asyncio.sleep(.2)
            try:await client.write_gatt_char(uuid(4),password.encode(),response=True)
            finally:password=''
            await asyncio.sleep(.2);await client.write_gatt_char(uuid(5),b'APPLY',response=True)
            until=time.monotonic()+80;promoted=False
            while time.monotonic()<until:
                try:
                    state=http.state('/api/network')
                    if state['revision']==0 and state['activeRevision']>before['activeRevision'] and state['wifiConnected']:promoted=True;break
                except (OSError,TimeoutError,RuntimeError):pass
                await asyncio.sleep(.5)
            check(promoted,'ble_stable_wifi_automatically_saved')
            check(all(http.state('/api/wireguard')[k]==wg[k] for k in ('enabled','address','mode','privateKeySet','presharedKeySet')),'ble_wifi_preserves_wireguard_configuration')
            check(inventory(http)==media,'ble_wifi_preserves_sd_media')
        await client.stop_notify(uuid(20))
        pin_before=http.state('/api/settings/ble')['pin']
        await client.write_gatt_char(uuid(15),b'REBOOT',response=True)
        await asyncio.sleep(8);http.wait_boot();http.login()
        check(http.state('/api/settings/ble')['pin']==pin_before,'ble_pin_persists_across_device_reboot');pin_before=''
        await client.connect()
        await client.write_gatt_char(uuid(3),b'ESP32-acceptance-staged-only',response=True);await asyncio.sleep(.3)
        check(http.state('/api/status')['ble']['authenticated'],'ble_bond_reconnects_authenticated_after_reboot')

def run(http,check,wifi=False):
    owned=set();created_bond=[False]
    try:asyncio.run(checks(http,check,owned,created_bond,wifi))
    finally:
        for session in owned:
            state=http.state('/api/timelapse')
            if state['active'] and state['session']==session:
                http.request('/api/timelapse/stop','POST')
                end=time.monotonic()+25
                while http.state('/api/timelapse')['active'] and time.monotonic()<end:time.sleep(.2)
            http.request('/api/timelapse/delete','POST',{'session':session})
        if created_bond[0]:
            check(http.request('/api/settings/ble','POST')[0]==202,'ble_pin_rotation_queued')
            time.sleep(1);check(not http.state('/api/settings/ble')['authenticated'],'ble_test_bond_cleared')


if __name__=='__main__':
    import argparse
    import json
    from pathlib import Path
    from appliance_client import Client
    root=Path(__file__).resolve().parents[2]
    parser=argparse.ArgumentParser();parser.add_argument('--wifi',action='store_true');parser.add_argument('--clear-test-bond',action='store_true');args=parser.parse_args()
    client=Client('192.168.1.181',root/'.tools/private/admin-password.txt')
    report={'passed':False,'checks':[]}
    def check(ok,name):
        report['checks'].append({'name':name,'passed':bool(ok)});print('BLE',ok,name,flush=True)
        if not ok:raise RuntimeError(name)
    try:
        client.login(progress=lambda p,s:print('LOGIN',p,s,flush=True))
        if args.clear_test_bond:
            # DeviceInformation is a snapshot: its pre-pair is_paired value does
            # not update after pairing. Clear the explicitly owned old Windows
            # test bond before asking the peripheral to rotate its PIN.
            async def remove_owned_bond():
                device=await BleakScanner.find_device_by_filter(lambda d,a:a.local_name=='XIAO-Camera-4C08',timeout=15)
                if device:
                    requester=await BluetoothLEDevice.from_bluetooth_address_async(int(device.address.replace(':',''),16))
                    try:
                        info=await DeviceInformation.create_from_id_async(requester.device_information.id)
                        if info.pairing.is_paired:await info.pairing.unpair_async()
                    finally:requester.close()
            asyncio.run(remove_owned_bond())
            check(client.request('/api/settings/ble','POST')[0]==202,'clear_prior_owned_test_bond');time.sleep(1)
        run(client,check,args.wifi);report['passed']=True
    finally:
        client.clear();(root/'.tools/hardware/ble-feature-report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
