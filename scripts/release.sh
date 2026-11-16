#!/usr/bin/env bash
# Build, test and package a release:
#   dist/lufs-vu-<version>-mpc.zip  =  "Slow Ops - VST - LUFS VU/" + INSTALL.txt + "LUFS VU User Guide.html" + LICENSE.txt
# The guide (docs/user-guide.html) is written into the zip as one self-contained file: its
# screenshots and fonts are inlined as data: URIs, so it opens offline and adds no folders that
# could end up inside "VST2 Plugins".
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
cd "$ROOT"
./scripts/test.sh
mkdir -p dist
python3 "$SDK/tools/package.py" plugin --out dist
python3 - <<'PY'
import base64, json, mimetypes, os, re, zipfile
root = os.getcwd()
version = json.load(open('plugin/plugin.json'))['version']
zpath = os.path.join('dist', 'lufs-vu-%s-mpc.zip' % version)
src = os.path.join(root, 'docs', 'user-guide.html')
html = open(src, encoding='utf-8').read()
def inline(m):
    ref = m.group(2)
    path = os.path.normpath(os.path.join(os.path.dirname(src), ref))
    mime = {'.ttf': 'font/ttf'}.get(os.path.splitext(path)[1]) or mimetypes.guess_type(path)[0]
    data = base64.b64encode(open(path, 'rb').read()).decode('ascii')
    return '%s"data:%s;base64,%s"' % (m.group(1), mime, data)
html = re.sub(r'(src=|url\()"((?:images|\.\./assets)/[^"]+)"', inline, html)
if re.search(r'(src=|url\()"(?!data:)', html):
    raise SystemExit('release: the guide still references an external file')
with zipfile.ZipFile(zpath, 'a', zipfile.ZIP_DEFLATED) as z:
    if 'LUFS VU User Guide.html' in z.namelist():
        raise SystemExit('release: the zip already has a guide')
    z.writestr('LUFS VU User Guide.html', html)
    z.write('LICENSE', 'LICENSE.txt')
print('release: %s (%.1f MB)' % (zpath, os.path.getsize(zpath) / 1e6))
PY
