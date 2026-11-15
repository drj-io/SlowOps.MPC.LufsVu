"""Shared geometry and palette for the VU Meter skin.

Imported by make_skin_art.py (Pillow artwork) and gen_skins.py (TUI.json / Q-Links.json),
so every rectangle, pivot and tick angle is defined exactly once.

Coordinates are MPC tab coordinates: a 1280 x 628 content area that starts 110 px
below the top of the 1280 x 800 screen (the MPC draws its own header above it).
"""
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKINS = os.path.join(ROOT, 'plugin', 'skins')
PARAMS_JSON = os.path.join(ROOT, 'plugin', 'build', 'params.json')
FONT_BOLD = os.path.join(ROOT, 'assets', 'fonts', 'TitilliumWeb-Bold.ttf')
FONT_SEMI = os.path.join(ROOT, 'assets', 'fonts', 'TitilliumWeb-SemiBold.ttf')
PREVIEW_DIR = os.path.join(ROOT, 'docs', 'images')

W, H = 1280, 628
SCREEN_TOP = 110

# ---------------------------------------------------------------------------- palette
NAVY = (17, 18, 31)
NAVY_2 = (24, 26, 43)
NAVY_CARD = (21, 22, 38)
WALNUT_DARK = (46, 20, 12)
WALNUT = (92, 42, 22)
WALNUT_LIGHT = (128, 64, 34)
BRASS_DARK = (122, 78, 30)
BRASS = (201, 146, 70)
BRASS_LIGHT = (246, 206, 132)
CREAM = (246, 229, 190)
CREAM_EDGE = (214, 178, 112)
INK = (24, 18, 14)
VU_RED = (205, 30, 40)
NEON = (255, 52, 72)
AMBER = (246, 170, 74)
AMBER_LIGHT = (255, 214, 140)
AMBER_TEXT = (255, 186, 92)
LAVENDER = (160, 168, 232)
DARK_TEXT = (42, 22, 8)

# Dial illumination per Light setting: (face centre colour, face edge colour, glow colour, glow strength)
LIGHTS = {
    'warm': ((255, 236, 196), (220, 176, 104), (255, 190, 90), 0.75),
    'cool': ((244, 242, 232), (196, 192, 176), (226, 236, 255), 0.45),
    'off':  ((198, 186, 160), (150, 134, 106), (0, 0, 0), 0.0),
}
LIGHT_ORDER = ['warm', 'cool', 'off', 'react', 'cycle']  # matches the plugin's Light choices
GLOW_LIGHTS = ['react', 'cycle']                 # drawn by the shared glow strip under a window overlay
STATIC_LIGHTS = ['warm', 'cool', 'off']          # baked into whole face images


# ---------------------------------------------------------------------------- React lighting
# The dial glow follows program heat (plugin: Heat.hpp). The glow layer is a smooth gradient,
# so it is rendered at reduced resolution and scaled up by the MPC; the printed scale, bezel
# and legends sit above it on a full-resolution transparent overlay, so text stays crisp.
# Glow strip frames (plugin "Glow" parameter): 0..23 React heat steps, 24..59 Cycle hue steps.
HEAT_FRAMES = 24
HUE_FRAMES = 36                                  # 10 degrees per step
GLOW_FRAMES = HEAT_FRAMES + HUE_FRAMES
REACT_GLOW_SCALE = 2                             # face glow strip at 1/2 resolution
REACT_HALO_SCALE = 4                             # cabinet halo strip at 1/4 resolution
HEAT_STOPS = [                                   # heat 0..1 -> backlight colour
    (0.00, (34, 78, 255)),                       # deep blue: quiet
    (0.22, (120, 186, 255)),                     # ice blue
    (0.42, (255, 176, 76)),                      # amber: getting warm
    (0.66, (255, 44, 40)),                       # red: 0 VU
    (0.86, (236, 36, 170)),                      # magenta
    (1.00, (150, 48, 255)),                      # purple: pinned
]


def heat_colour(t):
    t = max(0.0, min(1.0, t))
    for (t0, c0), (t1, c1) in zip(HEAT_STOPS, HEAT_STOPS[1:]):
        if t <= t1:
            k = (t - t0) / (t1 - t0)
            return tuple(int(round(c0[i] + (c1[i] - c0[i]) * k)) for i in range(3))
    return HEAT_STOPS[-1][1]


def heat_for_step(step):
    return step / float(HEAT_FRAMES - 1)


def hue_colour(t):
    """Cycle palette: a vivid trip round the hue wheel (t = 0..1, 0 = red)."""
    import colorsys
    r, g, b = colorsys.hsv_to_rgb(t % 1.0, 0.80, 1.0)
    return (int(round(r * 255)), int(round(g * 255)), int(round(b * 255)))


def glow_frame_colour(f):
    """(colour, intensity 0..1) of a glow-strip frame."""
    if f < HEAT_FRAMES:
        h = heat_for_step(f)
        return heat_colour(h), h
    return hue_colour((f - HEAT_FRAMES) / float(HUE_FRAMES)), 0.8
MODE_ORDER = ['stereo', 'mono']                # matches the plugin's Mode choices
NUM_VIEWS = 10                                 # View = mode * 5 + light
REF_LABELS = ['−20', '−18', '−14']  # matches the plugin's 0 VU choices


def argb(rgb, a=255):
    return '%02x%02x%02x%02x' % (a, rgb[0], rgb[1], rgb[2])


# ---------------------------------------------------------------------------- page regions
ORNAMENT = (0, 0, W, 56)
CABINET = (16, 58, 1248, 394)                  # walnut cabinet with neon edge
CABINET_INNER = (30, 70, 1220, 370)            # region covered by the per-view face images

STRIP_Y, STRIP_H = 466, 150                    # control strip cards
CARDS = {                                      # x, width (12 px gaps, 16 px page margins)
    'mode':  (16, 188),
    'cal':   (216, 276),
    'peak':  (504, 84),
    'light': (600, 370),
    'bypass': (982, 108),
    'output': (1102, 162),
}
CARD_TITLES = {'mode': 'MODE', 'cal': 'CALIBRATION', 'peak': 'PEAK', 'light': 'LIGHT', 'output': 'OUTPUT'}
BUTTON_Y, BUTTON_H = 530, 48


def card_rect(name):
    x, w = CARDS[name]
    return (x, STRIP_Y, w, STRIP_H)


def segment_buttons(card, n, pad=14, gap=8):
    """Rects of n equal buttons across a card."""
    x, w = CARDS[card]
    bw = (w - 2 * pad - (n - 1) * gap) // n
    return [(x + pad + i * (bw + gap), BUTTON_Y, bw, BUTTON_H) for i in range(n)]


MODE_BUTTONS = segment_buttons('mode', 2)
CAL_BUTTONS = segment_buttons('cal', 3)
PEAK_BUTTON = segment_buttons('peak', 1)[0]
LIGHT_BUTTONS = segment_buttons('light', 5)
BYPASS_BUTTON = (CARDS['bypass'][0], STRIP_Y, CARDS['bypass'][1], STRIP_H)
KNOB_SIZE = 76
KNOB = (CARDS['output'][0] + CARDS['output'][1] - KNOB_SIZE - 12, STRIP_Y + 52, KNOB_SIZE, KNOB_SIZE)
TRIM_CAPTION = (CARDS['output'][0] + 8, STRIP_Y + 60, 66, 24)        # burned "TRIM"
TRIM_VALUE = (CARDS['output'][0] + 2, STRIP_Y + 88, 78, 30)           # live Label
HALO = (0, 40, 1280, 432)                      # React cabinet halo region (1/4-res strip)

# ---------------------------------------------------------------------------- dials
# A dial "window" is the cream face inside a brass bezel. Needle sprites are positioned
# relative to the window origin.
STEREO_WINDOWS = [(54, 84, 566, 300), (660, 84, 566, 300)]
MONO_WINDOW = (54, 84, 1172, 300)
BEZEL = 14

# Lamps and plate under the dials (cabinet bottom rail).
LAMP = 30
STEREO_LAMPS = [(70, 404), (1180, 404)]        # top-left of each lamp (right caption sits left of its lamp)
LAMP_CAPTION_SIDE = {0: 'right', 1: 'left'}
MONO_LAMP = (70, 404)
LAMP_HIT_W = 130                               # reset-clip tap area (lamp + caption)
PLATE = (500, 400, 280, 40)

FULL_SCALE_VU = 3.0
TICKS_MAJOR = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3]
TICKS_MINOR = [-15, -6, -4, 0.5, 1.5, 2.5]


def deflection(vu):
    return 10.0 ** (vu / 20.0)


def fraction(vu):
    """Needle position as a fraction of the drawn sweep (0 = rest stop, 1 = +3 VU). Linear in voltage."""
    return deflection(vu) / deflection(FULL_SCALE_VU)


class Dial:
    """One meter face: pivot, radii and angle mapping, in window-local pixels.

    Angles are degrees from vertical, positive clockwise (to the right).
    -20 VU sits at angle_m20 and +3 VU at angle_p3; the rest stop is left of -20
    because the scale is linear in voltage (fraction(-20) = 0.0708).
    """

    def __init__(self, window, pivot, scale_r, angle_m20, angle_p3, needle_r0, needle_r1,
                 needle_w, label_size, legend, sub_legend, frames=None, sectors=None, clip_y=None):
        self.window = window
        self.pivot = pivot
        self.scale_r = scale_r
        f20 = fraction(-20)
        self.sweep = (angle_p3 - angle_m20) / (1.0 - f20)
        self.rest = angle_p3 - self.sweep
        self.needle_r0, self.needle_r1 = needle_r0, needle_r1
        self.needle_w = needle_w
        self.label_size = label_size
        self.legend, self.sub_legend = legend, sub_legend
        self.frames, self.sectors = frames, sectors
        self.clip_y = clip_y if clip_y is not None else window[3]   # needle is cut at the window bottom

    def angle_for_fraction(self, t):
        return self.rest + t * self.sweep

    def angle_for_vu(self, vu):
        return self.angle_for_fraction(fraction(vu))

    def angle_for_frame(self, f):
        return self.angle_for_fraction(f / float(self.frames - 1))

    def point(self, angle, r):
        a = math.radians(angle)
        return (self.pivot[0] + r * math.sin(a), self.pivot[1] - r * math.cos(a))


def stereo_dial(i):
    w = STEREO_WINDOWS[i]
    return Dial(w, pivot=(w[2] / 2.0, 292.0), scale_r=232, angle_m20=-50.0, angle_p3=50.0,
                needle_r0=40, needle_r1=262, needle_w=2.6, label_size=24,
                legend='VU', sub_legend='LEFT' if i == 0 else 'RIGHT',
                frames=STEREO_FRAMES, sectors=STEREO_SECTORS)


def mono_dial():
    w = MONO_WINDOW
    # A long, wide movement: the pivot sits far below the window so the scale spans the face.
    return Dial(w, pivot=(w[2] / 2.0, 960.0), scale_r=890, angle_m20=-29.0, angle_p3=29.0,
                needle_r0=0, needle_r1=916, needle_w=3.4, label_size=34,
                legend='VU', sub_legend='MONO  ·  L + R',
                frames=MONO_FRAMES, sectors=MONO_SECTORS)


# ---------------------------------------------------------------------------- parameters
def load_params():
    """Parameter list written by the native build (tools/build.py --target native)."""
    with open(PARAMS_JSON) as f:
        return json.load(f)['params']


def needle_spec(params):
    """{'L': (first_index, sectors, per), ...} derived from the plugin's own parameters.

    Each sector is an int display parameter 0..per (0 = blank frame); host_sim reports its
    steps as a choices list, so per = len(choices) - 1.
    """
    out = {}
    for key, prefix in (('L', 'L Needle '), ('R', 'R Needle '), ('M', 'M Needle ')):
        ps = sorted((p for p in params if p['name'].startswith(prefix)), key=lambda p: p['index'])
        out[key] = (ps[0]['index'], len(ps), len(ps[0]['choices']) - 1)
    return out


def index_of(params, name):
    for p in params:
        if p['name'] == name:
            return p['index']
    raise KeyError(name)


# Needle resolution; must match SlowOpsVUMeter.cpp (gen_skins.py checks it against params.json).
STEREO_FRAMES, STEREO_SECTORS = 128, 4
MONO_FRAMES, MONO_SECTORS = 256, 8


# ---------------------------------------------------------------------------- needle sprites
NEEDLE_MARGIN = 9          # px around the needle for antialiasing and its drop shadow
SHADOW_OFFSET = (3, 4)


def needle_segment(dial, angle):
    """Visible needle endpoints (window-local) at `angle`, cut at the window's bottom edge."""
    r0 = dial.needle_r0
    c = math.cos(math.radians(angle))
    if c > 1e-6:
        r_clip = (dial.pivot[1] - dial.clip_y) / c      # radius where the needle meets the bottom edge
        r0 = max(r0, r_clip)
    return dial.point(angle, r0), dial.point(angle, dial.needle_r1)


def sector_frames(dial, k):
    per = dial.frames // dial.sectors
    return list(range(k * per, (k + 1) * per))


def sector_box(dial, k):
    """Window-local integer box (x, y, w, h) holding every frame of sector k plus shadow."""
    xs, ys = [], []
    for f in sector_frames(dial, k):
        for (x, y) in needle_segment(dial, dial.angle_for_frame(f)):
            xs += [x, x + SHADOW_OFFSET[0]]
            ys += [y, y + SHADOW_OFFSET[1]]
    m = NEEDLE_MARGIN
    x0 = max(0, int(math.floor(min(xs) - m)))
    y0 = max(0, int(math.floor(min(ys) - m)))
    x1 = min(dial.window[2], int(math.ceil(max(xs) + m)))
    y1 = min(dial.window[3], int(math.ceil(max(ys) + m)))
    return (x0, y0, x1 - x0, y1 - y0)


def needle_file(kind, k):
    return 'needle_%s%d.png' % (kind, k + 1)


def decoded_needle_bytes():
    """Decoded RGBA bytes of all needle strips (stereo strips are shared by L and R)."""
    total = 0
    for dial in (stereo_dial(0), mono_dial()):
        per = dial.frames // dial.sectors
        for k in range(dial.sectors):
            _, _, w, h = sector_box(dial, k)
            total += w * h * (per + 1) * 4
    return total


def lamp_hit_rect(lamp_xy, side):
    """Tap area for RESET CLIP: the lamp plus its caption."""
    x, y = lamp_xy
    if side == 'right':
        return (x - 4, y - 4, LAMP_HIT_W, LAMP + 8)
    return (x + LAMP + 4 - LAMP_HIT_W, y - 4, LAMP_HIT_W, LAMP + 8)
