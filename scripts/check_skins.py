"""Validate plugin/skins against the plugin's real parameter list (standard library only).

Checks: every referenced PNG exists; filmstrip heights divide evenly into numFrames; needle
strips have sectors*per frames matching params.json; Buttons are exactly their image size
(the MPC draws them unscaled); every handle names an existing parameter; IndexedEnabling step
counts match replacementStrings; Q-Links only point at user controls; decoded needle memory.
"""
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_layout as L  # noqa: E402

errors = []


def fail(msg):
    errors.append(msg)


def png_size(name):
    path = os.path.join(L.SKINS, name)
    if not os.path.isfile(path):
        fail('missing image %s' % name)
        return None
    with open(path, 'rb') as f:
        head = f.read(24)
    if head[:8] != b'\x89PNG\r\n\x1a\n':
        fail('%s is not a PNG' % name)
        return None
    return struct.unpack('>II', head[16:24])


def main():
    params = L.load_params()
    n_params = len(params)
    by_index = {p['index']: p for p in params}
    tui = json.load(open(os.path.join(L.SKINS, 'TUI.json')))
    ql = json.load(open(os.path.join(L.SKINS, 'Q-Links.json')))
    page = tui['pageData']
    reps = {r['key']: r['value'] for r in page.get('replacementStrings', [])}

    for key, vals in reps.items():
        idx = int(key.split()[1])
        p = by_index.get(idx)
        if p is None:
            fail('replacementStrings for unknown %s' % key)
        elif p['choices'] and len(p['choices']) != len(vals):
            fail('%s: %d replacement strings but the plugin has %d choices' % (key, len(vals), len(p['choices'])))

    def check_handle(h, where):
        for m in re.finditer(r'Parameter (\d+)', h):
            if int(m.group(1)) >= n_params:
                fail('%s: %s is out of range (%d parameters)' % (where, h, n_params))
        m = re.match(r'IndexedEnabling/(\d+)/(\d+)/(Parameter \d+)$', h)
        r = re.match(r'RangedEnabling/(\d+)/(\d+)/(\d+)/(Parameter \d+)$', h)
        if m:
            lo, hi, n, target = int(m.group(1)), int(m.group(1)), int(m.group(2)), m.group(3)
        elif r:
            lo, hi, n, target = int(r.group(1)), int(r.group(2)), int(r.group(3)), r.group(4)
        if m or r:
            if target not in reps or len(reps[target]) != n:
                fail('%s: %s needs a %d-entry replacementStrings for %s' % (where, h, n, target))
            if not (0 <= lo <= hi < n):
                fail('%s: %s step out of range' % (where, h))

    spec = L.needle_spec(params)
    needle_frames = {}
    for key, (first, sectors, per) in spec.items():
        for k in range(sectors):
            needle_frames['Parameter %d' % (first + k)] = per + 1

    SCALED = {'glow_stereo.png': L.REACT_GLOW_SCALE, 'glow_mono.png': L.REACT_GLOW_SCALE}
    decoded = [0]
    widgets = [0]
    defs = {d['key']: d['value'] for d in page['componentDefinitions']['localComponentDefinitions']}

    def walk(entries, ox, oy, remap):
        for c in entries:
            cd, b = c['componentData'], c['bounds']
            name, kind, data = cd['name'], cd['type'], cd['data']
            x, y, w, h = [int(v) for v in b['bounds'].split()]
            x, y = x + ox, y + oy
            if x < 0 or y < 0 or x + w > L.W or y + h > L.H:
                fail('%s: bounds %s leave the 1280x628 tab' % (name, b['bounds']))
            if kind in defs:
                # a focusable control: its Data handle must reach a real parameter, and its children
                # are checked in tab coordinates
                m = {e['key']: e['value'] for e in c['handle remapping']['map']}
                target = remap.get(m.get('Data', 'Data'), m.get('Data', 'Data'))
                if b['acceptsHWFocus'] != 'Yes' or not target.startswith('Parameter '):
                    fail('%s: a focus group needs acceptsHWFocus Yes and Data -> a parameter' % name)
                focus_rects.append((name, x, y, w, h))
                walk(defs[kind]['componentsData'], x, y, {'Data': target})
                continue
            widgets[0] += 1
            if kind == 'Focus':
                continue
            if 'handleName' in data:
                data = dict(data, handleName=remap.get(data['handleName'], data['handleName']))
            for hnd in b.get('additionalInvalidatingHandles', []):
                check_handle(hnd, name)
                if b.get('showWhenDataModelInvalid') != 'Hide':
                    fail('%s: conditional widget must use showWhenDataModelInvalid Hide' % name)
            if 'handleName' in data:
                check_handle(data['handleName'], name)
            if kind == 'Image':
                size = png_size(data['image'])
                if size and data.get('imageType') == 'FilmStrip':
                    nf = data['numFrames']
                    if size[1] % nf:
                        fail('%s: %s height %d not divisible by %d frames' % (name, data['image'], size[1], nf))
                    else:
                        fw, fh = size[0], size[1] // nf
                        # React glow strips are smooth gradients drawn at reduced size and scaled UP on
                        # purpose (the LUFS glow by 4 x 0.8). Nothing may rely on scaling an image down.
                        k = SCALED.get(data['image'], 1) * (L.LUFS_SCALE if name == 'lufs glow' else 1)
                        if abs(fw * k - w) > 1 or abs(fh * k - h) > 1:
                            fail('%s: frame %dx%d (x%g) but bounds %dx%d' % (name, fw, fh, k, w, h))
                        decoded[0] += fw * fh * nf * 4
                    want = needle_frames.get(data['handleName'])
                    if want is not None and want != nf:
                        fail('%s: %d frames but the plugin sector has %d steps' % (name, nf, want))
                elif size:
                    decoded[0] += size[0] * size[1] * 4
                # The MPC never shrinks an image (it crops it), so plain images must be exactly their bounds.
                if size and data.get('imageType') != 'FilmStrip' and size != (w, h):
                    fail('%s: image %dx%d drawn into %dx%d (would be scaled)' % (name, size[0], size[1], w, h))
            elif kind == 'Button':
                for key in ('onImage', 'offImage'):
                    size = png_size(data[key])
                    if size and size != (w, h):
                        fail('%s: %s is %dx%d but the button is %dx%d (MPC draws buttons unscaled)' % (name, data[key], size[0], size[1], w, h))
            elif kind == 'Knob':
                size = png_size(data['filmStrip'])
                if size and (size[1] % data['numFrames'] or (size[0], size[1] // data['numFrames']) != (w, h)):
                    fail('%s: knob strip does not match %d frames of %dx%d' % (name, data['numFrames'], w, h))


    for tab in page['tabs']:
        bgimg = tab['componentDefinition']['backgroundData']['focussed'].get('image')
        if bgimg and png_size(bgimg) != (L.W, L.H):
            fail('tab background %s is not %dx%d' % (bgimg, L.W, L.H))
        focus_rects = []
        walk(tab['componentDefinition']['componentsData'], 0, 0, {})
        for i, (n1, x1, y1, w1, h1) in enumerate(focus_rects):
            for n2, x2, y2, w2, h2 in focus_rects[i + 1:]:
                if x1 < x2 + w2 and x2 < x1 + w1 and y1 < y2 + h2 and y2 < y1 + h1:
                    fail('%s: focus groups %s and %s overlap (a touch could reach the wrong one)' % (tab['tabName'], n1, n2))

    for tabmap in ql['Screen Mode Q-Links']['map']:
        for k, v in tabmap['Q-Links'].items():
            if v >= 0 and not by_index[v]['automatable']:
                fail('Q-Links: %s -> %s, which is read-only / a trigger' % (k, by_index[v]['name']))
    for k, v in ql['Program Mode Q-Links'].items():
        if v >= 0 and not by_index[v]['automatable']:
            fail('Program Q-Links: %s -> %s, which is read-only / a trigger' % (k, by_index[v]['name']))

    mib = L.decoded_needle_bytes() / 1048576.0
    all_mib = decoded[0] / 1048576.0
    if all_mib > 160:
        fail('all decoded images %.1f MiB exceed the 160 MiB ceiling' % all_mib)
    if mib > 100:
        fail('decoded needle strips %.1f MiB exceed the 100 MiB budget' % mib)
    if errors:
        for e in errors:
            print('  FAIL', e)
        sys.exit(1)
    print('  PASS %d widgets, %d parameters; decoded images: needles %.1f MiB, all images %.1f MiB'
          ' (stereo needle strips counted once per widget)' % (widgets[0], n_params, mib, all_mib))


if __name__ == '__main__':
    main()
