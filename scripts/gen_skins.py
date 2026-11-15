"""Generate plugin/skins/TUI.json and Q-Links.json from the plugin's real parameter list.

Run after a native build (which writes plugin/build/params.json):
    python3 scripts/gen_skins.py
Standard library only. Geometry comes from skin_layout.py; artwork from make_skin_art.py.

One METER tab:
  * tab background: ornament, walnut cabinet, control-strip cards (bg.png)
  * six face images (Mode x Light), shown by the plugin-computed View parameter
  * needle sector filmstrips (4 per stereo needle, 8 for mono), shown by Mode
  * clip lamps + transparent RESET CLIP tap areas, reference plates
  * control strip: MODE, CALIBRATION, PEAK, LIGHT, BYPASS, OUTPUT TRIM
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_layout as L  # noqa: E402

PARAMS = L.load_params()
IDX = {p['name']: p['index'] for p in PARAMS}
BY_INDEX = {p['index']: p for p in PARAMS}


def P(name):
    return 'Parameter %d' % IDX[name]


def rect(r):
    return '%d %d %d %d' % tuple(int(round(v)) for v in r)


def entry(name, kind, data, r, show='Show', only=None, focus='No'):
    b = {'version': 2 if only else 1, 'acceptsHWFocus': focus, 'showWhenDataModelInvalid': show,
         'whenVisible': 'Always', 'boundsType': 'Absolute', 'bounds': rect(r)}
    if only:
        b['additionalInvalidatingHandles'] = [only]
    return {'version': 2,
            'componentData': {'version': 1, 'name': name, 'type': kind, 'data': data},
            'handle remapping': {'version': 1, 'map': []},
            'bounds': b}


def image(name, img, r, only=None):
    return entry(name, 'Image', {'version': 2, 'imageType': 'Regular', 'colour': '0', 'image': img}, r,
                 show='Hide' if only else 'Show', only=only)


def filmstrip(name, img, frames, handle, r, only=None):
    return entry(name, 'Image', {'version': 2, 'imageType': 'FilmStrip', 'colour': '0', 'image': img,
                                 'numFrames': frames, 'handleName': handle}, r,
                 show='Hide' if only else 'Show', only=only)


def button(name, img, button_id, group, handle, r, only=None):
    return entry(name, 'Button', {'version': 2, 'onImage': img + '_on.png', 'offImage': img + '_off.png',
                                  'buttonId': button_id, 'numButtonsInGroup': group, 'handleName': handle,
                                  'gestureBehaviour': 'Instant'}, r, show='Hide', only=only)


def when(name, choice, count):
    return 'IndexedEnabling/%d/%d/%s' % (choice, count, P(name))


def check_needles():
    spec = L.needle_spec(PARAMS)
    expect = {'L': (L.STEREO_SECTORS, L.STEREO_FRAMES), 'R': (L.STEREO_SECTORS, L.STEREO_FRAMES),
              'M': (L.MONO_SECTORS, L.MONO_FRAMES)}
    for k, (first, sectors, per) in spec.items():
        es, ef = expect[k]
        if sectors != es or sectors * per != ef:
            sys.exit('gen_skins: %s needle is %d sectors x %d in the plugin but %d frames / %d sectors in skin_layout.py'
                     % (k, sectors, per, ef, es))
    return spec


def main():
    spec = check_needles()
    comps = []

    glow = IDX['Glow']
    if len(BY_INDEX[glow]['choices']) != L.GLOW_FRAMES:
        sys.exit('gen_skins: plugin Glow has %d steps, skin_layout.GLOW_FRAMES is %d'
                 % (len(BY_INDEX[glow]['choices']), L.GLOW_FRAMES))
    nl = len(L.LIGHT_ORDER)
    g0, g1 = L.LIGHT_ORDER.index(L.GLOW_LIGHTS[0]), L.LIGHT_ORDER.index(L.GLOW_LIGHTS[-1])

    # React / Cycle: cabinet halo in the glow colour (1/4-res strip, scaled up by the MPC)
    comps.append(filmstrip('glow halo', 'glow_halo.png', L.GLOW_FRAMES, P('Glow'), L.HALO,
                           only='RangedEnabling/%d/%d/%d/%s' % (g0, g1, nl, P('Light'))))

    # dial faces, one per View (Mode x Light); React and Cycle share a 1/2-res glow strip
    # under a transparent window overlay.
    for m, mode in enumerate(L.MODE_ORDER):
        for li, light in enumerate(L.STATIC_LIGHTS):
            comps.append(image('face %s %s' % (mode, light), 'face_%s_%s.png' % (mode, light), L.CABINET_INNER,
                               only=when('View', m * nl + li, L.NUM_VIEWS)))
        cond = 'RangedEnabling/%d/%d/%d/%s' % (m * nl + g0, m * nl + g1, L.NUM_VIEWS, P('View'))
        comps.append(filmstrip('glow %s' % mode, 'glow_%s.png' % mode, L.GLOW_FRAMES, P('Glow'), L.CABINET_INNER, only=cond))
        comps.append(image('face %s glow' % mode, 'face_%s_glow.png' % mode, L.CABINET_INNER, only=cond))

    # needles: sector filmstrips over each dial window
    def needles(key, kind, dial, mode_choice):
        first, sectors, per = spec[key]
        wx, wy = dial.window[0], dial.window[1]
        for k in range(sectors):
            x, y, w, h = L.sector_box(dial, k)
            comps.append(filmstrip('%s needle %d' % (key, k + 1), L.needle_file(kind, k), per + 1,
                                   'Parameter %d' % (first + k), (wx + x, wy + y, w, h), only=when('Mode', mode_choice, 2)))
    needles('L', 's', L.stereo_dial(0), 0)
    needles('R', 's', L.stereo_dial(1), 0)
    needles('M', 'm', L.mono_dial(), 1)

    # clip lamps and their RESET CLIP tap areas
    lamps = [('L Clip', L.STEREO_LAMPS[0], L.LAMP_CAPTION_SIDE[0], 0),
             ('R Clip', L.STEREO_LAMPS[1], L.LAMP_CAPTION_SIDE[1], 0),
             ('Input Clip', L.MONO_LAMP, 'right', 1)]
    for name, (x, y), side, mode_choice in lamps:
        comps.append(filmstrip(name.lower(), 'lamp.png', 2, P(name), (x, y, L.LAMP, L.LAMP), only=when('Mode', mode_choice, 2)))
        comps.append(button(name.lower() + ' reset', 'hit', 1, 1, P('Reset Clip'), L.lamp_hit_rect((x, y), side),
                            only=when('Mode', mode_choice, 2)))

    # reference plate
    for i in range(3):
        comps.append(image('plate %d' % i, 'plate_%d.png' % i, L.PLATE, only=when('0 VU', i, 3)))

    # control strip
    for i, r in enumerate(L.MODE_BUTTONS):
        comps.append(button('mode %d' % i, 'mode_%d' % i, i, 2, P('Mode'), r))
    for i, r in enumerate(L.CAL_BUTTONS):
        comps.append(button('cal %d' % i, 'cal_%d' % i, i, 3, P('0 VU'), r))
    comps.append(button('peak', 'peak', 1, 1, P('Peak Hold'), L.PEAK_BUTTON))
    for i, r in enumerate(L.LIGHT_BUTTONS):
        comps.append(button('light %d' % i, 'light_%d' % i, i, len(L.LIGHT_BUTTONS), P('Light'), r))
    comps.append(button('bypass', 'bypass', 1, 1, P('Bypass'), L.BYPASS_BUTTON))
    comps.append(entry('trim knob', 'Knob', {'version': 3, 'knobType': 'FilmStrip', 'filmStrip': 'knob_trim.png',
                                             'numFrames': 127, 'invert': False, 'dragOrientation': 'Vertical',
                                             'handleName': P('Trim')}, L.KNOB, show='Hide', focus='Yes'))
    comps.append(entry('trim value', 'Label', {
        'version': 1,
        'textStyle': {'version': 1, 'font': {'version': 1, 'name': 'Titillium Web', 'style': 'SemiBold', 'height': 22.0},
                      'colour': L.argb(L.CREAM), 'justification': 'horizontallyCentred verticallyCentred', 'case': 'Original'},
        'type': 'Value', 'handleName': P('Trim')}, L.TRIM_VALUE, show='Hide'))

    bg = {'version': 1, 'colour': L.argb(L.NAVY), 'image': 'bg.png'}
    tab = {'version': 1, 'tabName': 'METER', 'fnKeyIndex': 0, 'fnKeySubIndex': 0,
           'componentDefinition': {'version': 2, 'actions': [],
                                   'backgroundData': {'version': 1, 'focussed': bg, 'unfocussed': bg},
                                   'ignoreMousePresses': False, 'disableCoarseDataWheel': False,
                                   'componentsData': comps},
           'qlinkBoundsData': []}

    # Discrete steps for every choice/bool parameter a widget or IndexedEnabling depends on.
    reps = []
    for name in ('Mode', '0 VU', 'Peak Hold', 'Light', 'Bypass', 'Reset Clip', 'View', 'L Clip', 'R Clip', 'Input Clip'):
        p = BY_INDEX[IDX[name]]
        reps.append({'key': P(name), 'value': p['choices']})

    tui = {'pageData': {'version': 1,
                        'componentDefinitions': {'version': 2, 'importFiles': [], 'localComponentDefinitions': []},
                        'replacementStrings': reps,
                        'info': {'version': 1, 'type': 'CompleteDescription'},
                        'tabs': [tab]}}

    # Q-Links: top row follows the strip left to right; read-only displays and the trigger stay unassigned.
    q = {'Q-Link %d' % i: -1 for i in range(1, 17)}
    for ql, name in ((13, 'Mode'), (14, '0 VU'), (15, 'Peak Hold'), (16, 'Light'), (9, 'Bypass'), (10, 'Trim')):
        q['Q-Link %d' % ql] = IDX[name]
    prog = {'Q-Link %d' % i: -1 for i in range(1, 17)}
    for i, name in enumerate(('Mode', '0 VU', 'Peak Hold', 'Light', 'Bypass', 'Trim')):
        prog['Q-Link %d' % (i + 1)] = IDX[name]
    qlinks = {'version': 4, 'info': {'version': 1, 'type': 'CompleteDescription'},
              'Screen Mode Q-Links': {'version': 4, 'map': [{'Tab': 1, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': q}]},
              'Program Mode Q-Links': prog}

    os.makedirs(L.SKINS, exist_ok=True)
    with open(os.path.join(L.SKINS, 'TUI.json'), 'w') as f:
        json.dump(tui, f, indent=1)
    with open(os.path.join(L.SKINS, 'Q-Links.json'), 'w') as f:
        json.dump(qlinks, f, indent=1)
    print('gen_skins: %d widgets on METER' % len(comps))


if __name__ == '__main__':
    main()
