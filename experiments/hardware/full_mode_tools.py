"""Shared bounded helpers for real WG-B hardware acceptance."""
import ipaddress
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
from urllib.parse import quote

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from validate_avi import validate


def expected_exit(client):
    config=(ROOT/'XIAO_ESP32_Camera.conf').read_text(encoding='utf-8-sig')
    endpoint=re.search(r'^\s*Endpoint\s*=\s*([^\r\n]+)',config,re.M)
    if not endpoint:raise RuntimeError('No configured endpoint')
    hostname=endpoint.group(1).strip().rsplit(':',1)[0]
    for name in ('PrivateKey','PublicKey','PresharedKey'):
        value=re.search(r'^\s*'+name+r'\s*=\s*(\S+)',config,re.M)
        if value:client.private_values.append(value.group(1).encode())
    config=''
    return {str(ipaddress.IPv4Address(item[4][0])) for item in socket.getaddrinfo(hostname,None,socket.AF_INET,socket.SOCK_DGRAM)}


def inventory(client):
    dates=[];items={};cursor=''
    for _ in range(32):
        data=client.state('/api/media/dates'+('?before='+quote(cursor,safe='') if cursor else ''))
        if data.get('enumerationTruncated'):raise RuntimeError('Incomplete dates')
        dates.extend(item['date'] for item in data['items'])
        if not data['more']:break
        cursor=data['nextBefore']
    else:raise RuntimeError('Date enumeration bound')
    for date in dates:
        cursor=''
        for _ in range(128):
            data=client.state('/api/media?date='+date+('&before='+quote(cursor,safe='') if cursor else ''))
            if data.get('enumerationTruncated'):raise RuntimeError('Incomplete media')
            items.update({item['path']:item['bytes'] for item in data['items']})
            if not data['more']:break
            cursor=data['nextBefore']
        else:raise RuntimeError('Media enumeration bound')
    return items


def decode_jpeg(data,target):
    target.write_bytes(data)
    result=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(target),'-f','null','-'],capture_output=True,timeout=30)
    return data.startswith(b'\xff\xd8') and result.returncode==0


def decode_avi(target):
    probe=subprocess.run(['C:/Program Files/ffmpeg/bin/ffprobe.exe','-v','error','-show_streams','-show_format','-of','json',str(target)],capture_output=True,timeout=30,check=True)
    result=validate(json.loads(probe.stdout))
    decoded=subprocess.run(['C:/Program Files/ffmpeg/bin/ffmpeg.exe','-v','error','-xerror','-i',str(target),'-f','null','-'],capture_output=True,timeout=45)
    result['whole_decode_passed']=decoded.returncode==0
    return result
