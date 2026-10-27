"""Regression test using the user's actual v0.6.0 HTTP client, without its GUI.

Only writes a unique probe directory; verifies stations.txt remains unchanged.
"""
import argparse
import hashlib
import http.client
import importlib.util
import json
import sys
import time
import urllib.error
import urllib.parse
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--client', required=True)
p.add_argument('--host', required=True)
p.add_argument('--output', required=True)
args = p.parse_args()
spec = importlib.util.spec_from_file_location('moon_original_file_manager', args.client)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
client = module.HttpSpiFFSClient()
client.connect(args.host)
original_stations = client.read_file('/stations.txt')
directory = f'/__moon_probe_{time.time_ns()}'
target = directory + '/alkönyvtár/ellenőrzés.bin'
result = {'clientVersion': module.APP_VERSION, 'tests': []}


def upload(data):
    list(client.write_file(target, data))
    assert client.read_file(target, expected_size=len(data)) == data


try:
    for size in (0, 6500, 384 * 1024):
        data = bytes((i * 37) % 256 for i in range(size))
        upload(data)
        result['tests'].append(f'{size} bytes: upload + readback hash match')
        print(result['tests'][-1], flush=True)
    original_probe = b'Original probe must survive an interrupted replacement.\x00' * 50
    upload(original_probe)
    boundary = 'MoonInterruptedUploadProbe'
    head = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="partial.bin"\r\n'
            'Content-Type: application/octet-stream\r\n\r\n').encode()
    url = urllib.parse.urlsplit(client.base_url)
    conn = http.client.HTTPConnection(url.netloc, timeout=15)
    conn.putrequest('POST', '/upload?' + urllib.parse.urlencode({'path': target}))
    conn.putheader('Content-Type', f'multipart/form-data; boundary={boundary}')
    conn.putheader('Content-Length', str(len(head) + 100000))
    conn.endheaders()
    conn.send(head + b'partial data')
    conn.close()
    time.sleep(2)
    assert client.read_file_retry(target, expected_size=len(original_probe)) == original_probe
    result['tests'].append('interrupted replacement: original probe preserved')
    print(result['tests'][-1], flush=True)
    payload = b'--MoonInvalid\r\nContent-Disposition: form-data; name="file"; filename="invalid.bin"\r\n\r\nx\r\n--MoonInvalid--\r\n'
    try:
        client._request('POST', '/upload?path=%2F..%2Fstations.txt', data=payload,
                        headers={'Content-Type': 'multipart/form-data; boundary=MoonInvalid'})
        raise AssertionError('Invalid destination was accepted')
    except module.ProtoError as error:
        assert 'HTTP 400' in str(error), str(error)
    result['tests'].append('invalid parent path: rejected')
    assert client.read_file('/stations.txt') == original_stations
    result['stationsSha256'] = hashlib.sha256(original_stations).hexdigest()
    result['stationsUnchanged'] = True
    print('Original stations.txt unchanged', flush=True)
finally:
    try:
        client.rmdir(directory)
        result['probeRemoved'] = True
    except Exception as error:
        raise RuntimeError(f'Probe cleanup failed: {directory}: {error}') from error
Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print('File manager upload regression tests passed.')
