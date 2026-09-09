#!/usr/bin/env python3
"""Upload release assets for WallDesk versions to GitHub."""
import json
import os
import sys
import urllib.request
import urllib.error
import urllib.parse

TOKEN = os.environ.get('GH_TOKEN')
if not TOKEN:
    print('Set GH_TOKEN', file=sys.stderr)
    sys.exit(1)

REPO = 'Gitsnod/WallDesk'
DIST = os.path.join(os.path.dirname(__file__), '..', 'dist')
VERSIONS = ['4.1.0', '4.2.0', '4.2.1', '4.3.0', '4.4.0']

def api_req(url, method='GET', data=None, headers=None):
    h = {'Authorization': f'Bearer {TOKEN}', 'Accept': 'application/vnd.github+json'}
    if headers:
        h.update(headers)
    req = urllib.request.Request(url, method=method, data=data, headers=h)
    with urllib.request.urlopen(req, timeout=300) as resp:
        return json.loads(resp.read().decode('utf-8'))

def main():
    releases = api_req(f'https://api.github.com/repos/{REPO}/releases')
    rel_map = {r['tag_name']: r for r in releases}
    for v in VERSIONS:
        tag = f'v{v}'
        rel = rel_map.get(tag)
        if not rel:
            print(f'Skip missing release {tag}')
            continue
        existing = {a['name'] for a in rel.get('assets', [])}
        base = f'https://uploads.github.com/repos/{REPO}/releases/{rel["id"]}/assets'
        files = [
            (f'WallDesk-{v}-setup.exe', 'application/x-msdownload'),
            (f'WallDesk-{v}-win64.zip', 'application/zip'),
        ]
        for fname, ctype in files:
            if fname in existing:
                print(f'{tag}: {fname} already exists, skip')
                continue
            path = os.path.join(DIST, fname)
            if not os.path.isfile(path):
                print(f'{tag}: missing file {path}', file=sys.stderr)
                continue
            url = f'{base}?name={urllib.parse.quote(fname)}'
            with open(path, 'rb') as f:
                data = f.read()
            print(f'{tag}: uploading {fname} ({len(data)} bytes)...')
            try:
                api_req(url, method='POST', data=data, headers={'Content-Type': ctype})
                print(f'{tag}: uploaded {fname}')
            except urllib.error.HTTPError as e:
                print(f'{tag}: failed {fname}: {e.code} {e.read().decode()}', file=sys.stderr)

if __name__ == '__main__':
    main()
