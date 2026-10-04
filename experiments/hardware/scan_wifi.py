"""Request a fresh Windows WLAN scan without changing profiles or credentials.

API reference: https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanscan
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import subprocess
import time
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--interface-guid", required=True)
parser.add_argument("--ssid", required=True)
args = parser.parse_args()
api = c.WinDLL("wlanapi")
api.WlanOpenHandle.argtypes = [w.DWORD, c.c_void_p, c.POINTER(w.DWORD), c.POINTER(w.HANDLE)]
api.WlanOpenHandle.restype = w.DWORD
api.WlanScan.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_void_p, c.c_void_p]
api.WlanScan.restype = w.DWORD
api.WlanCloseHandle.argtypes = [w.HANDLE, c.c_void_p]
api.WlanCloseHandle.restype = w.DWORD
handle = w.HANDLE()
version = w.DWORD()
opened = api.WlanOpenHandle(2, None, c.byref(version), c.byref(handle))
if opened:
    raise SystemExit(f"WlanOpenHandle failed: {opened}")
try:
    guid = (c.c_ubyte * 16).from_buffer_copy(uuid.UUID(args.interface_guid).bytes_le)
    result = api.WlanScan(handle, c.byref(guid), None, None, None)
    if result:
        raise SystemExit(f"WlanScan failed: {result}")
    time.sleep(5)
    listing = subprocess.run(["netsh", "wlan", "show", "networks", "mode=bssid"], capture_output=True, timeout=10)
    text = listing.stdout.decode("utf-8", "replace")
    print(f"Fresh scan requested; target_visible={args.ssid in text}")
    if args.ssid in text:
        section = text.split(args.ssid, 1)[1].split("\nSSID ", 1)[0]
        print(section.strip())
finally:
    api.WlanCloseHandle(handle, None)
