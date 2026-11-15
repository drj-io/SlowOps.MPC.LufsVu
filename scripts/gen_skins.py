"""Generate plugin/skins/TUI.json and Q-Links.json from the plugin's real parameter list.

Run after a native build (which writes plugin/build/params.json):
    python3 scripts/gen_skins.py
Standard library only. Geometry comes from skin_layout.py; artwork from make_skin_art.py.

METER tab:
  * tab background: ornament, walnut cabinet, control-strip cards (bg.png)
  * face images per Light (Warm/Cool/Off) plus a glow strip + overlay for React/Cycle,
    chosen by the plugin-computed View parameter
  * needle sector filmstrips (4 per needle), clip lamps + RESET CLIP tap areas, plate,
    PHASE meter, MAX readouts
  * control strip: PEAK | BYPASS | OUTPUT (trim value, trim knob)
LUFS tab:
  * the same dials at 0.8 scale (shared needle and glow strips; caption-free faces), clip lamps
  * smoked-glass tuning strip: INTEGRATED | SHORT-TERM | TRUE PEAK MAX | GAIN REDUCTION
    Labels, plus AUTO status / auto gain / momentary / elapsed
    (CLEAR and RUN/HOLD sit at the strip's left end: they control the measurement)
  * GAIN card: AUTO | TARGET < > | TRIM knob;  OUTPUT card: LIMITER | CEILING < >;  BYPASS
SETUP tab: CALIBRATION | CLIP LEVEL, then LIGHT (STATIC | REACT + palette | CYCLE + rate).
The plugin is stereo only; the retired mono parameters have no widgets.
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
    # host_sim only lists the steps of small int ranges; when it does, they must match. The frame
    # layout itself is pinned by scenario_host (palette offsets, hue range, flash = last frame).
    if BY_INDEX[glow]['choices'] and len(BY_INDEX[glow]['choices']) != L.GLOW_FRAMES:
        sys.exit('gen_skins: plugin Glow has %d steps, skin_layout.GLOW_FRAMES is %d'
                 % (len(BY_INDEX[glow]['choices']), L.GLOW_FRAMES))
    g0, g1 = L.LIGHT_ORDER.index(L.GLOW_LIGHTS[0]), L.LIGHT_ORDER.index(L.GLOW_LIGHTS[-1])

    # The cabinet border never changes colour: React / Cycle only light the dial windows.
    # dial faces, one per View (= Light; the plugin is stereo only). React and Cycle share a
    # 1/4-res glow strip under a transparent window overlay.
    for li, light in enumerate(L.STATIC_LIGHTS):
        comps.append(image('face %s' % light, 'face_stereo_%s.png' % light, L.CABINET_INNER,
                           only=when('View', li, L.NUM_VIEWS)))
    cond = 'RangedEnabling/%d/%d/%d/%s' % (g0, g1, L.NUM_VIEWS, P('View'))
    comps.append(filmstrip('glow', 'glow_stereo.png', L.GLOW_FRAMES, P('Glow'), L.CABINET_INNER, only=cond))
    comps.append(image('face glow', 'face_stereo_glow.png', L.CABINET_INNER, only=cond))

    # needles: sector filmstrips over each dial window
    def needles(key, kind, dial):
        first, sectors, per = spec[key]
        wx, wy = dial.window[0], dial.window[1]
        for k in range(sectors):
            x, y, w, h = L.sector_box(dial, k)
            comps.append(filmstrip('%s needle %d' % (key, k + 1), L.needle_file(kind, k), per + 1,
                                   'Parameter %d' % (first + k), (wx + x, wy + y, w, h)))
    needles('L', 's', L.stereo_dial(0))
    needles('R', 's', L.stereo_dial(1))

    # clip lamps and their RESET CLIP tap areas
    lamps = [('L Clip', L.STEREO_LAMPS[0], L.LAMP_CAPTION_SIDE[0]),
             ('R Clip', L.STEREO_LAMPS[1], L.LAMP_CAPTION_SIDE[1])]
    for name, (x, y), side in lamps:
        comps.append(filmstrip(name.lower(), 'lamp.png', 2, P(name), (x, y, L.LAMP, L.LAMP)))
        comps.append(button(name.lower() + ' reset', 'hit', 1, 1, P('Reset Clip'), L.lamp_hit_rect((x, y), side)))

    # reference plate
    for i in range(3):
        comps.append(image('plate %d' % i, 'plate_%d.png' % i, L.PLATE, only=when('0 VU', i, 3)))

    # control strip: PEAK | BYPASS | OUTPUT
    comps.append(button('peak', 'peak', 1, 1, P('Peak Hold'), L.PEAK_BUTTON))
    comps.append(button('bypass', 'bypass', 1, 1, P('Bypass'), L.BYPASS_BUTTON))
    comps.append(entry('trim knob', 'Knob', {'version': 3, 'knobType': 'FilmStrip', 'filmStrip': 'knob_trim.png',
                                             'numFrames': 127, 'invert': False, 'dragOrientation': 'Vertical',
                                             'handleName': P('Trim')}, L.KNOB, show='Hide', focus='Yes'))
    comps.append(entry('trim value', 'Label', {
        'version': 1,
        'textStyle': {'version': 1, 'font': {'version': 1, 'name': 'Titillium Web', 'style': 'SemiBold', 'height': 30.0},
                      'colour': L.argb(L.CREAM), 'justification': 'horizontallyCentred verticallyCentred', 'case': 'Original'},
        'type': 'Value', 'handleName': P('Trim')}, L.TRIM_VALUE, show='Hide'))

    # phase correlation
    comps.append(filmstrip('phase', 'corr.png', L.CORR_STEPS + 1, P('Correlation'), L.CORR))

    # MAX VU readouts in each dial's lower-right corner
    def max_label(name, param, window):
        comps.append(entry(name, 'Label', {
            'version': 1,
            'textStyle': {'version': 1, 'font': {'version': 1, 'name': 'Titillium Web', 'style': 'SemiBold', 'height': 17.0},
                          'colour': L.argb(L.INK), 'justification': 'right verticallyCentred', 'case': 'Original'},
            'type': 'Value', 'handleName': P(param)}, L.max_label_rect(window), show='Hide'))
    max_label('max L', 'L Max', L.STEREO_WINDOWS[0])
    max_label('max R', 'R Max', L.STEREO_WINDOWS[1])

    def label(name, param, r, size, colour, just, style='SemiBold'):
        return entry(name, 'Label', {
            'version': 1,
            'textStyle': {'version': 1, 'font': {'version': 1, 'name': 'Titillium Web', 'style': style, 'height': float(size)},
                          'colour': L.argb(colour), 'justification': just + ' verticallyCentred', 'case': 'Original'},
            'type': 'Value', 'handleName': P(param)}, r, show='Hide')

    # LUFS tab: the METER dials at LUFS_SCALE. Faces and needle strips are rendered at that size
    # (the MPC never shrinks an image); only the soft 1/4-res glow strip is scaled up.
    lufs = []
    for li, light in enumerate(L.STATIC_LIGHTS):
        lufs.append(image('lufs face %s' % light, 'face_lufs_%s.png' % light, L.LUFS_FACE, only=when('View', li, L.NUM_VIEWS)))
    lufs.append(filmstrip('lufs glow', 'glow_stereo.png', L.GLOW_FRAMES, P('Glow'), L.LUFS_FACE, only=cond))
    lufs.append(image('lufs face glow', 'face_lufs_glow.png', L.LUFS_FACE, only=cond))
    for key, dial in (('L', L.stereo_dial(0)), ('R', L.stereo_dial(1))):
        first, sectors, per = spec[key]
        wx, wy = dial.window[0], dial.window[1]
        for k in range(sectors):
            x, y, w, h = L.sector_box(dial, k)
            lufs.append(filmstrip('lufs %s needle %d' % (key, k + 1), L.lufs_needle_file(k), per + 1,
                                  'Parameter %d' % (first + k), L.lufs_needle_rect(dial, k)))
    for name, param, window in (('lufs max L', 'L Max', L.STEREO_WINDOWS[0]), ('lufs max R', 'R Max', L.STEREO_WINDOWS[1])):
        lufs.append(label(name, param, L.lufs_max_rect(window), L.label_px(14), L.INK, 'right'))
    for i, name in enumerate(('L Clip', 'R Clip')):
        x, y = L.LUFS_LAMPS[i]
        lufs.append(filmstrip('side ' + name.lower(), 'lamp.png', 2, P(name), (x, y, L.LAMP, L.LAMP)))
        lufs.append(button('lufs %s reset' % name.lower(), 'hit_lufs', 1, 1, P('Reset Clip'), L.lufs_lamp_hit(i)))
    # tuning strip readouts
    for i, param in enumerate(L.READOUT_PARAMS):
        lufs.append(label('readout %s' % param.lower(), param, L.readout_value_rect(i), L.label_px(40), L.READOUT_AMBER, 'right', 'Bold'))
    lufs.append(label('auto state', 'Auto State', L.STATUS_RECT, L.label_px(16), L.STATUS_AMBER, 'right'))
    lufs.append(label('auto gain', 'Auto Gain', L.AUTO_GAIN_RECT, L.label_px(16), L.STATUS_AMBER, 'right'))
    lufs.append(label('momentary', 'Momentary', L.MOMENTARY_RECT, L.label_px(16), L.STATUS_AMBER, 'horizontallyCentred'))
    lufs.append(label('elapsed', 'Elapsed', L.ELAPSED_RECT, L.label_px(16), L.STATUS_AMBER, 'left'))
    # measurement controls, at the left end of the readout strip
    lufs.append(button('clear', 'clear', 1, 1, P('Clear'), L.CLEAR_BUTTON))
    lufs.append(button('run hold', 'hold', 1, 1, P('Measure'), L.HOLD_BUTTON))
    # controls
    # GAIN card (AUTO | TARGET | TRIM) and OUTPUT card (LIMITER | CEILING)
    lufs.append(button('lufs auto', 'lufs_auto', 1, 1, P('LUFS Auto'), L.lufs_toggle('gain')))
    lufs.append(button('limiter', 'lufs_lim', 1, 1, P('Limiter'), L.lufs_toggle('output')))
    lufs.append(button('lufs bypass', 'bypass_lufs', 1, 1, P('Bypass'), L.LUFS_BYPASS))   # the one global Bypass
    for card, value, down, up in (('gain', 'Target', 'Target Down', 'Target Up'), ('output', 'Ceiling', 'Ceiling Down', 'Ceiling Up')):
        lft, pill, rgt = L.lufs_stepper(card)
        lufs.append(button(down.lower(), 'arrow_l', 1, 1, P(down), lft))
        lufs.append(label(value.lower() + ' value', value, pill, L.label_px(24), L.CREAM, 'horizontallyCentred'))
        lufs.append(button(up.lower(), 'arrow_r', 1, 1, P(up), rgt))
    lufs.append(entry('lufs trim knob', 'Knob', {'version': 3, 'knobType': 'FilmStrip', 'filmStrip': 'knob_trim.png',
                                                 'numFrames': 127, 'invert': False, 'dragOrientation': 'Vertical',
                                                 'handleName': P('Trim')}, L.LUFS_KNOB, show='Hide', focus='Yes'))
    lufs.append(label('lufs trim value', 'Trim', L.LUFS_TRIM_VALUE, L.label_px(26), L.CREAM, 'horizontallyCentred'))

    def make_tab(name, key, bgimg, children):
        bg = {'version': 1, 'colour': L.argb(L.NAVY), 'image': bgimg}
        return {'version': 1, 'tabName': name, 'fnKeyIndex': key, 'fnKeySubIndex': 0,
                'componentDefinition': {'version': 2, 'actions': [],
                                        'backgroundData': {'version': 1, 'focussed': bg, 'unfocussed': bg},
                                        'ignoreMousePresses': False, 'disableCoarseDataWheel': False,
                                        'componentsData': children},
                'qlinkBoundsData': []}

    # SETUP tab: CALIBRATION | CLIP LEVEL, then the LIGHT box (STATIC | REACT + palette | CYCLE + rate)
    setup = []
    for i, r in enumerate(L.CAL_BUTTONS):
        setup.append(button('cal %d' % i, 'cal_%d' % i, i, len(L.CAL_BUTTONS), P('0 VU'), r))
    for i, r in enumerate(L.LIGHT_BUTTONS):
        setup.append(button('light %d' % i, 'light_%d' % i, i, len(L.LIGHT_BUTTONS), P('Light'), r))
    for i, r in enumerate(L.CLIP_BUTTONS):
        setup.append(button('clip level %d' % i, 'clip_%d' % i, i, len(L.CLIP_BUTTONS), P('Clip Level'), r))
    for i, r in enumerate(L.RATE_BUTTONS):
        setup.append(button('cycle rate %d' % i, 'rate_%d' % i, i, len(L.RATE_BUTTONS), P('Cycle Rate'), r))
    for i, r in enumerate(L.PALETTE_BUTTONS):
        setup.append(button('palette %d' % i, 'pal_%d' % i, i, len(L.PALETTE_BUTTONS), P('Palette'), r))

    tabs = [make_tab('METER', 0, 'bg.png', comps), make_tab('LUFS', 1, 'bg_lufs.png', lufs),
            make_tab('SETUP', 2, 'bg_setup.png', setup)]

    # Discrete steps for every choice/bool parameter a widget or IndexedEnabling depends on.
    reps = []
    for name in ('0 VU', 'Peak Hold', 'Light', 'Bypass', 'Reset Clip', 'View', 'L Clip', 'R Clip',
                 'Clip Level', 'Cycle Rate', 'Palette', 'LUFS Auto', 'Limiter', 'Measure', 'Clear',
                 'Target Down', 'Target Up', 'Ceiling Down', 'Ceiling Up'):
        p = BY_INDEX[IDX[name]]
        reps.append({'key': P(name), 'value': p['choices']})

    tui = {'pageData': {'version': 1,
                        'componentDefinitions': {'version': 2, 'importFiles': [], 'localComponentDefinitions': []},
                        'replacementStrings': reps,
                        'info': {'version': 1, 'type': 'CompleteDescription'},
                        'tabs': tabs}}

    # Q-Links: top row follows the strip left to right; read-only displays and the trigger stay unassigned.
    q = {'Q-Link %d' % i: -1 for i in range(1, 17)}
    for ql, name in ((13, 'Peak Hold'), (14, 'Bypass'), (15, 'Trim')):
        q['Q-Link %d' % ql] = IDX[name]
    q2 = {'Q-Link %d' % i: -1 for i in range(1, 17)}
    for ql, name in ((13, '0 VU'), (14, 'Clip Level'), (15, 'Light'), (16, 'Palette'), (9, 'Cycle Rate')):
        q2['Q-Link %d' % ql] = IDX[name]
    q3 = {'Q-Link %d' % i: -1 for i in range(1, 17)}   # LUFS tab (q2 is SETUP, now the third tab)
    for ql, name in ((13, 'Target'), (14, 'Ceiling'), (15, 'Trim'), (16, 'LUFS Auto'), (9, 'Limiter'), (10, 'Measure'),
                     (11, 'Bypass')):
        q3['Q-Link %d' % ql] = IDX[name]
    prog = {'Q-Link %d' % i: -1 for i in range(1, 17)}
    for i, name in enumerate(('Trim', 'Bypass', 'Peak Hold', '0 VU', 'Clip Level', 'Light', 'Palette', 'Cycle Rate',
                              'LUFS Auto', 'Target', 'Limiter', 'Ceiling', 'Measure')):
        prog['Q-Link %d' % (i + 1)] = IDX[name]
    qlinks = {'version': 4, 'info': {'version': 1, 'type': 'CompleteDescription'},
              'Screen Mode Q-Links': {'version': 4, 'map': [
                  {'Tab': 1, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': q},
                  {'Tab': 2, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': q3},
                  {'Tab': 3, 'SubTab': 1, 'Bank Direction': 'Column', 'Q-Links': q2}]},
              'Program Mode Q-Links': prog}

    os.makedirs(L.SKINS, exist_ok=True)
    with open(os.path.join(L.SKINS, 'TUI.json'), 'w') as f:
        json.dump(tui, f, indent=1)
    with open(os.path.join(L.SKINS, 'Q-Links.json'), 'w') as f:
        json.dump(qlinks, f, indent=1)
    print('gen_skins: %d widgets on METER, %d on LUFS, %d on SETUP' % (len(comps), len(lufs), len(setup)))


if __name__ == '__main__':
    main()
