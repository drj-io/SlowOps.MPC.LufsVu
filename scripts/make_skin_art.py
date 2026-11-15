"""Draw every PNG of the VU Meter skin (needs Pillow).

    python3 -m venv build/venv && build/venv/bin/pip install pillow
    build/venv/bin/python scripts/make_skin_art.py

Everything is original artwork generated here from code; geometry comes from skin_layout.py.
Captions are burned into the images because fixed-text Labels do not render on the MPC.
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skin_layout as L  # noqa: E402

SS = 4                      # supersampling factor for shapes and text
OUT = L.SKINS
random.seed(1931)


# ---------------------------------------------------------------------------- helpers
def font(size, semi=False):
    return ImageFont.truetype(L.FONT_SEMI if semi else L.FONT_BOLD, int(round(size)))


def rr_mask(w, h, rect, radius, ss=SS):
    """Antialiased rounded-rectangle mask (L) of size w x h."""
    m = Image.new('L', (w * ss, h * ss), 0)
    x, y, rw, rh = rect
    ImageDraw.Draw(m).rounded_rectangle([x * ss, y * ss, (x + rw) * ss - 1, (y + rh) * ss - 1],
                                        radius=radius * ss, fill=255)
    return m.resize((w, h), Image.LANCZOS)


def vgrad(w, h, top, bottom):
    g = Image.linear_gradient('L').resize((w, h))
    return Image.merge('RGB', [g.point(lambda v, a=top[i], b=bottom[i]: int(a + (b - a) * v / 255.0)) for i in range(3)])


def radial(w, h, cx, cy, rx, ry, inner, outer, power=1.0):
    """Elliptical radial gradient image (RGB)."""
    small = Image.new('L', (max(2, w // 4), max(2, h // 4)))
    px = small.load()
    sw, sh = small.size
    for y in range(sh):
        for x in range(sw):
            dx = (x * 4 - cx) / rx
            dy = (y * 4 - cy) / ry
            d = min(1.0, math.sqrt(dx * dx + dy * dy)) ** power
            px[x, y] = int(d * 255)
    m = small.resize((w, h), Image.BICUBIC)
    return Image.merge('RGB', [m.point(lambda v, a=inner[i], b=outer[i]: int(a + (b - a) * v / 255.0)) for i in range(3)])


def paste(dst, src, mask, xy=(0, 0)):
    layer = Image.new('RGBA', dst.size, (0, 0, 0, 0))
    s = src.convert('RGBA')
    a = mask if mask.mode == 'L' else mask.getchannel('A')
    s.putalpha(ImageChops.multiply(s.getchannel('A'), a) if src.mode == 'RGBA' else a)
    layer.paste(s, xy)
    dst.alpha_composite(layer)


def text_image(text, size, colour, slant=0.18, tracking=0.0, semi=False, shadow=None):
    """RGBA image of text, optionally slanted (faux italic) and letter-spaced, supersampled."""
    f = font(size * SS, semi)
    chars = list(text)
    widths = [f.getlength(c) for c in chars]
    track = tracking * size * SS
    tw = int(sum(widths) + track * max(0, len(chars) - 1)) + 4 * SS
    asc, desc = f.getmetrics()
    th = asc + desc + 2 * SS
    pad = int(th * slant) + 4 * SS
    img = Image.new('RGBA', (tw + 2 * pad, th + 2 * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    x = pad
    for c, cw in zip(chars, widths):
        d.text((x, SS), c, font=f, fill=colour)
        x += cw + track
    if slant:
        img = img.transform(img.size, Image.AFFINE, (1, slant, -slant * img.size[1] * 0.55, 0, 1, 0), Image.BICUBIC)
    img = img.crop(img.getbbox() or (0, 0, 1, 1))
    img = img.resize((max(1, img.size[0] // SS), max(1, img.size[1] // SS)), Image.LANCZOS)
    if shadow:
        sh = Image.new('RGBA', (img.size[0] + 6, img.size[1] + 6), (0, 0, 0, 0))
        blk = Image.new('RGBA', img.size, shadow)
        blk.putalpha(ImageChops.multiply(img.getchannel('A'), Image.new('L', img.size, shadow[3])))
        sh.alpha_composite(blk, (3, 4))
        sh = sh.filter(ImageFilter.GaussianBlur(1.6))
        sh.alpha_composite(img, (1, 1))
        img = sh
    return img


def put_text(dst, text, size, colour, centre, **kw):
    img = text_image(text, size, colour, **kw)
    dst.alpha_composite(img, (int(round(centre[0] - img.size[0] / 2.0)), int(round(centre[1] - img.size[1] / 2.0))))
    return img.size


def glow_rect(size, rect, radius, colour, blur, width, alpha=255):
    """A blurred neon outline (RGBA) of a rounded rect."""
    w, h = size
    m = Image.new('L', (w, h), 0)
    x, y, rw, rh = rect
    ImageDraw.Draw(m).rounded_rectangle([x, y, x + rw - 1, y + rh - 1], radius=radius, outline=alpha, width=width)
    m = m.filter(ImageFilter.GaussianBlur(blur))
    out = Image.new('RGBA', (w, h), colour + (0,))
    out.putalpha(m)
    return out


def save(img, name):
    img.save(os.path.join(OUT, name), optimize=True)


# ---------------------------------------------------------------------------- background
def wood(w, h):
    noise = Image.effect_noise((max(8, w // 48), h), 64).resize((w, h), Image.BICUBIC)
    fine = Image.effect_noise((max(8, w // 12), h), 40).resize((w, h), Image.BICUBIC)
    n = ImageChops.blend(noise, fine, 0.35)
    n = ImageOps.autocontrast(n, cutoff=2)
    return ImageOps.colorize(n, black=L.WALNUT_DARK, white=L.WALNUT_LIGHT, mid=L.WALNUT)


def ornament(bg):
    d = ImageDraw.Draw(bg)
    cx, base = 640, 46
    big = Image.new('RGBA', (L.W * SS, 60 * SS), (0, 0, 0, 0))
    bd = ImageDraw.Draw(big)
    for i, y in enumerate((26, 33, 40)):
        inset = i * 40
        for x0, x1 in ((250 + inset, 600), (680, 1030 - inset)):
            bd.line([(x0 * SS, y * SS), (x1 * SS, y * SS)], fill=L.BRASS + (255 - i * 50,), width=int(1.6 * SS))
    for a in range(-72, 73, 12):
        r = 34 if a % 24 == 0 else 26
        ang = math.radians(a)
        bd.line([(cx * SS, base * SS), ((cx + r * math.sin(ang)) * SS, (base - r * math.cos(ang)) * SS)],
                fill=L.BRASS_LIGHT + (255,), width=int(2 * SS))
    bd.ellipse([(cx - 7) * SS, (base - 7) * SS, (cx + 7) * SS, (base + 7) * SS], fill=L.BRASS + (255,))
    big = big.resize((L.W, 60), Image.LANCZOS)
    glow = big.filter(ImageFilter.GaussianBlur(4))
    bg.alpha_composite(glow)
    bg.alpha_composite(big)
    del d


def card(bg, rect, title=None):
    x, y, w, h = rect
    bg.alpha_composite(glow_rect(bg.size, rect, 16, L.NEON, 6, 4, 150))
    paste(bg, vgrad(w, h, (26, 26, 44), (16, 16, 30)), rr_mask(w, h, (0, 0, w, h), 16), (x, y))
    bg.alpha_composite(glow_rect(bg.size, (x + 1, y + 1, w - 2, h - 2), 15, L.NEON, 0.6, 2, 230))
    if title:
        img = text_image(title, 23, L.AMBER_TEXT + (255,), tracking=0.04)
        bg.alpha_composite(img, (x + 22 if w > 140 else x + (w - img.size[0]) // 2, y + 16))


def make_background():
    bg = Image.new('RGBA', (L.W, L.H), L.NAVY + (255,))
    grid = Image.new('RGBA', (L.W, L.H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grid)
    for x in range(0, L.W, 32):
        gd.line([(x, 0), (x, L.H)], fill=(60, 64, 110, 18))
    for y in range(0, L.H, 32):
        gd.line([(0, y), (L.W, y)], fill=(60, 64, 110, 18))
    bg.alpha_composite(grid)
    ornament(bg)

    # cabinet: neon halo, walnut, brass inner line
    cx, cy, cw, ch = L.CABINET
    bg.alpha_composite(glow_rect(bg.size, L.CABINET, 30, L.NEON, 10, 6, 170))
    bg.alpha_composite(glow_rect(bg.size, L.CABINET, 30, (255, 140, 60), 3, 3, 120))
    wd = wood(cw, ch)
    shade = vgrad(cw, ch, (255, 220, 190), (120, 80, 60))
    wd = ImageChops.multiply(wd, shade)
    paste(bg, wd, rr_mask(cw, ch, (0, 0, cw, ch), 30), (cx, cy))
    bg.alpha_composite(glow_rect(bg.size, (cx + 1, cy + 1, cw - 2, ch - 2), 29, (255, 120, 90), 0.7, 2, 230))
    bg.alpha_composite(glow_rect(bg.size, (cx + 8, cy + 8, cw - 16, ch - 16), 24, L.BRASS, 0.5, 1, 140))
    # bottom rail rules beside the plate
    rail = Image.new('RGBA', bg.size, (0, 0, 0, 0))
    rd = ImageDraw.Draw(rail)
    for x0, x1 in ((290, 480),):
        rd.line([(x0, 421), (x1, 421)], fill=L.BRASS + (200,), width=2)
    bg.alpha_composite(rail)

    for name in ('peak', 'output'):
        card(bg, L.card_rect(name), L.CARD_TITLES[name])
    tx, ty, tw, th = L.TRIM_CAPTION
    put_text(bg, 'TRIM', 14, (196, 170, 150, 255), (tx + tw / 2.0, ty + th / 2.0), tracking=0.08)
    return bg


def make_setup_background():
    """SETUP tab: CALIBRATION and CLIP LEVEL cards, then one LIGHT box grouped by light type:
    STATIC (warm/cool/off) | REACT + palette | CYCLE + rate."""
    bg = Image.new('RGBA', (L.W, L.H), L.NAVY + (255,))
    grid = Image.new('RGBA', (L.W, L.H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grid)
    for x in range(0, L.W, 32):
        gd.line([(x, 0), (x, L.H)], fill=(60, 64, 110, 18))
    for y in range(0, L.H, 32):
        gd.line([(0, y), (L.W, y)], fill=(60, 64, 110, 18))
    bg.alpha_composite(grid)
    ornament(bg)
    for key, r in L.SETUP_CARDS.items():
        card(bg, r, L.SETUP_TITLES[key])
    lx, ly, lw, lh = L.SETUP_CARDS['light']
    rules = Image.new('RGBA', bg.size, (0, 0, 0, 0))
    rd = ImageDraw.Draw(rules)
    keys = ['static', 'react', 'cycle']
    for k in keys:
        cx, cw = L.light_col(k)
        t = text_image(L.LIGHT_COLUMN_TITLES[k], 19, L.CREAM + (230,), tracking=0.18)
        bg.alpha_composite(t, (cx, ly + 66))
        if k in L.LIGHT_SUBTITLES:
            st = text_image(L.LIGHT_SUBTITLES[k], 15, (190, 170, 150, 255), tracking=0.2)
            bg.alpha_composite(st, (cx, L.LIGHT_SUB_Y - 30))
            # bracket tying the sub-options to their light button
            rd.line([(cx - 10, L.LIGHT_BTN_Y + 26), (cx - 10, L.LIGHT_SUB_Y + 26)], fill=L.BRASS + (160,), width=2)
            rd.line([(cx - 10, L.LIGHT_SUB_Y + 26), (cx - 4, L.LIGHT_SUB_Y + 26)], fill=L.BRASS + (160,), width=2)
            rd.line([(cx - 10, L.LIGHT_BTN_Y + 26), (cx - 4, L.LIGHT_BTN_Y + 26)], fill=L.BRASS + (160,), width=2)
        cap = text_image(L.LIGHT_CAPTIONS[k], 16, (170, 160, 190, 255), slant=0.1, semi=True)
        bg.alpha_composite(cap, (cx, ly + lh - 44))
    for a, b in zip(keys, keys[1:]):              # brass dividers between the columns
        x = (L.light_col(a)[0] + L.light_col(a)[1] + L.light_col(b)[0]) // 2
        rd.line([(x, ly + 60), (x, ly + lh - 20)], fill=L.BRASS + (110,), width=2)
    bg.alpha_composite(rules)
    return bg


# ---------------------------------------------------------------------------- dial faces
def draw_scale(face, dial, ox, oy, light):
    """Ticks, arc, red zone and numerals, drawn at SS into `face` (an RGBA at SS scale)."""
    d = ImageDraw.Draw(face)
    px, py = (ox + dial.pivot[0]) * SS, (oy + dial.pivot[1]) * SS
    R = dial.scale_r * SS
    ink = L.INK + (255,) if light != 'off' else (40, 32, 26, 255)

    def arc(r, a0, a1, colour, width):
        d.arc([px - r, py - r, px + r, py + r], a0 - 90, a1 - 90, fill=colour, width=int(width))

    a20, a0, a3 = dial.angle_for_vu(-20), dial.angle_for_vu(0), dial.angle_for_vu(3)
    arc(R, a20 - 0.4, a0, ink, 2.6 * SS)
    arc(R + 5 * SS, a0, a3 + 0.3, L.VU_RED + (255,), 9.5 * SS)
    tick_long = 24 if dial.scale_r < 400 else 30
    for vu in L.TICKS_MAJOR + L.TICKS_MINOR:
        major = vu in L.TICKS_MAJOR
        a = math.radians(dial.angle_for_vu(vu))
        r_in = R - (tick_long if major else tick_long * 0.55) * SS
        r_out = R + (2 if major else 0) * SS
        col = (L.VU_RED + (255,)) if vu > 0 else ink
        w = (3.2 if major else 2.0) * SS
        d.line([(px + r_in * math.sin(a), py - r_in * math.cos(a)), (px + r_out * math.sin(a), py - r_out * math.cos(a))],
               fill=col, width=int(w))
    # numerals, rotated to follow the arc
    for vu in L.TICKS_MAJOR:
        text = ('+%d' % vu) if vu > 0 else ('−%d' % -vu if vu < 0 else '0')
        col = (L.VU_RED + (255,)) if vu > 0 else ink
        img = text_image(text, dial.label_size * SS / SS, col, slant=0.12)
        img = img.resize((img.size[0] * SS, img.size[1] * SS), Image.LANCZOS)
        ang = dial.angle_for_vu(vu)
        img = img.rotate(-ang, expand=True, resample=Image.BICUBIC)
        r = (dial.scale_r + dial.label_size * 1.05) * SS
        a = math.radians(ang)
        cx, cy = px + r * math.sin(a), py - r * math.cos(a)
        face.alpha_composite(img, (int(cx - img.size[0] / 2), int(cy - img.size[1] / 2)))


def face_fill(ww, wh, centre, edge, glow_col, glow_k):
    """The illuminated dial paper: radial falloff plus lamp glows along the bottom edge."""
    face_rgb = radial(ww, wh, ww / 2.0, wh * 0.95, ww * 0.62, wh * 1.05, centre, edge, 1.4)
    if glow_k > 0:
        g = Image.new('L', (ww, wh), 0)
        gd = ImageDraw.Draw(g)
        for fx in (0.22, 0.5, 0.78) if ww > 800 else (0.25, 0.75):
            gd.ellipse([ww * fx - 90, wh - 40, ww * fx + 90, wh + 60], fill=int(255 * min(1.0, glow_k)))
        g = g.filter(ImageFilter.GaussianBlur(38))
        face_rgb = Image.composite(Image.new('RGB', (ww, wh), glow_col), face_rgb, g)
    return face_rgb


def mix(a, b, k):
    return tuple(int(round(a[i] + (b[i] - a[i]) * k)) for i in range(3))


def glow_light(col, k):
    """Dial illumination for a coloured backlight of intensity k 0..1: tinted paper,
    saturated edges, coloured lamps along the bottom."""
    centre = mix((252, 247, 240), col, 0.30 + 0.12 * k)
    edge = mix(mix(col, (0, 0, 0), 0.38), (40, 26, 40), 0.15)
    glow = mix(col, (255, 255, 255), 0.18)
    return centre, edge, glow, 0.62 + 0.33 * k


def draw_dial(img, dial, light):
    """Bezel + illuminated face + printed scale for one dial, into the face image `img`."""
    ox, oy = dial.window[0] - L.CABINET_INNER[0], dial.window[1] - L.CABINET_INNER[1]
    ww, wh = dial.window[2], dial.window[3]
    b = L.BEZEL
    # bezel: dark outer lip, brass frame with highlight, inner shadow
    bx, by, bw, bh = ox - b, oy - b, ww + 2 * b, wh + 2 * b
    shadow = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle([bx - 2, by + 2, bx + bw + 2, by + bh + 8], radius=58, fill=(0, 0, 0, 170))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(8)))
    brass = vgrad(bw, bh, L.BRASS_LIGHT, L.BRASS_DARK)
    hl = radial(bw, bh, bw * 0.3, bh * 0.05, bw * 0.7, bh * 0.9, (255, 236, 180), L.BRASS_DARK, 0.8)
    brass = ImageChops.blend(brass, hl, 0.45)
    paste(img, brass, rr_mask(bw, bh, (0, 0, bw, bh), 56), (bx, by))
    paste(img, vgrad(bw - 8, bh - 8, L.BRASS_DARK, (255, 220, 150)), rr_mask(bw - 8, bh - 8, (0, 0, bw - 8, bh - 8), 52), (bx + 4, by + 4))

    # face: illumination per Light setting (React: left transparent, the glow strip shows through)
    face = Image.new('RGBA', (ww * SS, wh * SS), (0, 0, 0, 0))
    local = Image.new('RGBA', (ww, wh), (0, 0, 0, 0))
    if light != 'glow':
        paste(local, face_fill(ww, wh, *L.LIGHTS[light]), Image.new('L', (ww, wh), 255))
    else:   # punch the window out of the brass plate so the React glow strip shows through
        hole = Image.new('L', img.size, 0)
        hole.paste(rr_mask(ww, wh, (0, 0, ww, wh), 44), (ox, oy))
        img.putalpha(ImageChops.subtract(img.getchannel('A'), hole))
    # printed scale at SS, in window-local coordinates
    draw_scale(face, dial, 0, 0, light)
    # legends
    ink = L.INK + (255,) if light != 'off' else (40, 32, 26, 255)
    big = 72 if ww < 800 else 92
    vu_y = wh * (0.53 if ww < 800 else 0.6)
    leg = text_image(dial.legend, big, ink, slant=0.16)
    leg = leg.resize((leg.size[0] * SS, leg.size[1] * SS), Image.LANCZOS)
    face.alpha_composite(leg, (int(ww * SS / 2 - leg.size[0] / 2), int(vu_y * SS - leg.size[1] / 2)))
    sub = text_image(dial.sub_legend, 24 if ww < 800 else 26, ink, slant=0.16, tracking=0.32)
    sub = sub.resize((sub.size[0] * SS, sub.size[1] * SS), Image.LANCZOS)
    sy = (vu_y + big * 0.62) * SS
    face.alpha_composite(sub, (int(ww * SS / 2 - sub.size[0] / 2), int(sy - sub.size[1] / 2)))
    fd = ImageDraw.Draw(face)
    for side in (-1, 1):
        x0 = ww * SS / 2 + side * (sub.size[0] / 2 + 14 * SS)
        fd.line([(x0, sy), (x0 + side * 34 * SS, sy)], fill=ink, width=int(2.4 * SS))
    if dial.needle_r0 > 0:   # visible pivot: brass half-dome on the bottom edge
        px, py = dial.pivot[0] * SS, dial.pivot[1] * SS
        r = (dial.needle_r0 - 4) * SS
        dome = Image.new('RGBA', face.size, (0, 0, 0, 0))
        dd = ImageDraw.Draw(dome)
        for i in range(24):
            k = i / 23.0
            col = tuple(int(L.BRASS_DARK[c] + (L.BRASS_LIGHT[c] - L.BRASS_DARK[c]) * (1 - abs(k - 0.35) * 1.4)) for c in range(3))
            rr = r * (1 - k * 0.6)
            dd.ellipse([px - rr, py - rr, px + rr, py + rr], fill=col + (255,))
        face.alpha_composite(dome)
    face = face.resize((ww, wh), Image.LANCZOS)
    local.alpha_composite(face)
    # inner bezel shadow and glass sheen
    inner = Image.new('L', (ww, wh), 0)
    ImageDraw.Draw(inner).rounded_rectangle([0, 0, ww - 1, wh - 1], radius=44, outline=255, width=10)
    inner = inner.filter(ImageFilter.GaussianBlur(9))
    local.alpha_composite(Image.merge('RGBA', [Image.new('L', (ww, wh), 40)] * 3 + [inner.point(lambda v: v * 0.7)]))
    sheen = Image.linear_gradient('L').rotate(35, expand=False).resize((ww, wh)).point(lambda v: max(0, 70 - v) // 3)
    local.alpha_composite(Image.merge('RGBA', [Image.new('L', (ww, wh), 255)] * 3 + [sheen]))
    paste(img, local, rr_mask(ww, wh, (0, 0, ww, wh), 44), (ox, oy))


def lamp_caption(img, xy, text, side='right'):
    x, y = xy[0] - L.CABINET_INNER[0], xy[1] - L.CABINET_INNER[1]
    plate = text_image(text, 22, L.CREAM + (255,), tracking=0.04, shadow=(0, 0, 0, 200))
    tx = x + L.LAMP + 10 if side == 'right' else x - 10 - plate.size[0]
    img.alpha_composite(plate, (tx, y + (L.LAMP - plate.size[1]) // 2))


def make_face(mode, light):
    cw, ch = L.CABINET_INNER[2], L.CABINET_INNER[3]
    img = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
    if mode == 'stereo':
        for i in range(2):
            draw_dial(img, L.stereo_dial(i), light)
        lamp_caption(img, L.STEREO_LAMPS[0], 'CLIP')
        lamp_caption(img, L.STEREO_LAMPS[1], 'CLIP', 'left')
    else:
        draw_dial(img, L.mono_dial(), light)
        raise ValueError('mono view was retired in v0.2')
    return img


# ---------------------------------------------------------------------------- React lighting
def windows_for(mode):
    return [L.stereo_dial(0).window, L.stereo_dial(1).window] if mode == 'stereo' else [L.MONO_WINDOW]


GLOW_BLEED = 10    # glow extends under the bezel so the scaled-up strip's soft edge is hidden


def glow_frame(mode, f):
    """Full-resolution face glow for glow-strip frame f (transparent outside the windows)."""
    cw, ch = L.CABINET_INNER[2], L.CABINET_INNER[3]
    col, k = L.glow_frame_colour(f)
    img = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
    b = GLOW_BLEED
    for (wx, wy, ww, wh) in windows_for(mode):
        fill = face_fill(ww, wh, *glow_light(col, k)).resize((ww + 2 * b, wh + 2 * b), Image.BICUBIC)
        paste(img, fill, rr_mask(ww + 2 * b, wh + 2 * b, (0, 0, ww + 2 * b, wh + 2 * b), 44 + b),
              (wx - L.CABINET_INNER[0] - b, wy - L.CABINET_INNER[1] - b))
    return img


def halo_frame(f):
    """Full-resolution cabinet halo in the frame's colour (drawn over the static neon edge)."""
    hx, hy, hw, hh = L.HALO
    col, heat = L.glow_frame_colour(f)
    cab = (L.CABINET[0] - hx, L.CABINET[1] - hy, L.CABINET[2], L.CABINET[3])
    img = Image.new('RGBA', (hw, hh), (0, 0, 0, 0))
    strength = 0.85 + 0.15 * heat
    img.alpha_composite(glow_rect((hw, hh), cab, 30, col, 14, 12, int(255 * strength)))
    img.alpha_composite(glow_rect((hw, hh), cab, 30, col, 6, 6, int(255 * strength)))
    img.alpha_composite(glow_rect((hw, hh), cab, 30, mix(col, (255, 255, 255), 0.35), 3, 4, int(255 * strength)))
    return img


def make_glow():
    n = L.GLOW_FRAMES
    gs, hs = L.REACT_GLOW_SCALE, L.REACT_HALO_SCALE
    cw, ch = L.CABINET_INNER[2], L.CABINET_INNER[3]
    for mode in ['stereo']:
        fw, fh = cw // gs, ch // gs
        strip = Image.new('RGBA', (fw, fh * n), (0, 0, 0, 0))
        for f in range(n):
            strip.alpha_composite(glow_frame(mode, f).resize((fw, fh), Image.LANCZOS), (0, f * fh))
        save(strip, 'glow_%s.png' % mode)
    hw, hh = L.HALO[2] // hs, L.HALO[3] // hs
    strip = Image.new('RGBA', (hw, hh * n), (0, 0, 0, 0))
    for f in range(n):
        strip.alpha_composite(halo_frame(f).resize((hw, hh), Image.LANCZOS), (0, f * hh))
    save(strip, 'glow_halo.png')


# ---------------------------------------------------------------------------- needles
def needle_frame(dial, angle, box):
    x0, y0, bw, bh = box
    img = Image.new('RGBA', (bw * SS, bh * SS), (0, 0, 0, 0))
    (ax, ay), (bx, by) = L.needle_segment(dial, angle)
    a = math.radians(angle)
    def P(x, y, dx=0, dy=0):
        return ((x - x0 + dx) * SS, (y - y0 + dy) * SS)
    sh = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).line([P(ax, ay, *L.SHADOW_OFFSET), P(bx, by, *L.SHADOW_OFFSET)], fill=(20, 10, 0, 95),
                            width=int(dial.needle_w * 1.4 * SS))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(2.2 * SS)))
    d = ImageDraw.Draw(img)
    d.line([P(ax, ay), P(bx, by)], fill=L.INK + (255,), width=int(dial.needle_w * SS))
    # red pointer tip through the scale, like a classic VU needle
    r_tip0 = dial.scale_r - (26 if dial.scale_r < 400 else 34)
    tx, ty = dial.point(angle, r_tip0)
    d.line([P(tx, ty), P(bx, by)], fill=(200, 24, 34, 255), width=int(dial.needle_w * 0.9 * SS))
    img = img.resize((bw, bh), Image.LANCZOS)
    if dial.clip_y < dial.window[3] or True:   # cut cleanly at the window bottom
        cut = dial.clip_y - y0
        if cut < bh:
            img.paste((0, 0, 0, 0), (0, max(0, int(cut)), bw, bh))
    return img


def make_needles(kind, dial):
    per = dial.frames // dial.sectors
    for k in range(dial.sectors):
        box = L.sector_box(dial, k)
        strip = Image.new('RGBA', (box[2], box[3] * (per + 1)), (0, 0, 0, 0))   # frame 0 is blank
        for j, f in enumerate(L.sector_frames(dial, k)):
            strip.alpha_composite(needle_frame(dial, dial.angle_for_frame(f), box), (0, (j + 1) * box[3]))
        save(strip, L.needle_file(kind, k))


# ---------------------------------------------------------------------------- controls
def button(w, h, label, style, on, size=21):
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    r = 11
    if style == 'seg':
        if on:
            img.alpha_composite(glow_rect((w, h), (2, 2, w - 4, h - 4), r, L.AMBER, 2.5, 3, 200))
            paste(img, vgrad(w - 6, h - 6, L.AMBER_LIGHT, L.AMBER), rr_mask(w - 6, h - 6, (0, 0, w - 6, h - 6), r), (3, 3))
            hi = Image.new('RGBA', (w, h), (0, 0, 0, 0))
            ImageDraw.Draw(hi).rounded_rectangle([6, 5, w - 7, h // 2], radius=r - 3, fill=(255, 255, 255, 60))
            img.alpha_composite(hi.filter(ImageFilter.GaussianBlur(2)))
            col = L.DARK_TEXT + (255,)
        else:
            paste(img, vgrad(w - 6, h - 6, (36, 38, 66), (20, 21, 38)), rr_mask(w - 6, h - 6, (0, 0, w - 6, h - 6), r), (3, 3))
            img.alpha_composite(glow_rect((w, h), (3, 3, w - 6, h - 6), r, (110, 104, 160), 0.5, 2, 200))
            col = L.LAVENDER + (255,)
        put_text(img, label, size, col, (w / 2.0, h / 2.0 + 1), tracking=0.03)
    elif style in ('react', 'cycle') or style.startswith('pal:'):
        if style.startswith('pal:'):
            name = style[4:]
            palette = lambda t, n=name: L.heat_colour(t, n)
        else:
            palette = L.heat_colour if style == 'react' else L.hue_colour
        spectrum = Image.new('RGB', (w - 6, h - 6))
        sd = ImageDraw.Draw(spectrum)
        for x in range(w - 6):
            sd.line([(x, 0), (x, h - 7)], fill=palette(x / float(w - 7) * (1.0 if style == 'react' else 0.92)))
        if on:
            img.alpha_composite(glow_rect((w, h), (2, 2, w - 4, h - 4), r, palette(0.7), 2.5, 3, 220))
            shade = vgrad(w - 6, h - 6, (255, 255, 255), (170, 170, 170))
            paste(img, ImageChops.multiply(spectrum, shade), rr_mask(w - 6, h - 6, (0, 0, w - 6, h - 6), r), (3, 3))
            hi = Image.new('RGBA', (w, h), (0, 0, 0, 0))
            ImageDraw.Draw(hi).rounded_rectangle([6, 5, w - 7, h // 2], radius=r - 3, fill=(255, 255, 255, 70))
            img.alpha_composite(hi.filter(ImageFilter.GaussianBlur(2)))
            put_text(img, label, size, (255, 255, 255, 255), (w / 2.0, h / 2.0 + 1), tracking=0.03, shadow=(20, 0, 30, 200))
        else:
            paste(img, vgrad(w - 6, h - 6, (36, 38, 66), (20, 21, 38)), rr_mask(w - 6, h - 6, (0, 0, w - 6, h - 6), r), (3, 3))
            edge = Image.new('RGBA', (w, h), (0, 0, 0, 0))
            ring = Image.new('L', (w, h), 0)
            ImageDraw.Draw(ring).rounded_rectangle([3, 3, w - 4, h - 4], radius=r, outline=230, width=2)
            sp = Image.new('RGBA', (w, h)); sp.paste(spectrum.resize((w, h)))
            sp.putalpha(ring)
            edge.alpha_composite(sp)
            img.alpha_composite(edge)
            txt = text_image(label, size, (255, 255, 255, 255), tracking=0.03)
            grad = spectrum.resize(txt.size).convert('RGBA')
            grad.putalpha(txt.getchannel('A'))
            img.alpha_composite(grad, ((w - txt.size[0]) // 2, (h - txt.size[1]) // 2 + 1))
    elif style == 'bypass':
        if on:
            img.alpha_composite(glow_rect((w, h), (3, 3, w - 6, h - 6), 16, L.NEON, 5, 5, 230))
            paste(img, radial(w - 12, h - 12, (w - 12) / 2.0, (h - 12) * 0.35, w * 0.8, h * 0.9,
                              (255, 92, 104), (150, 14, 34)), rr_mask(w - 12, h - 12, (0, 0, w - 12, h - 12), 14), (6, 6))
            col = (255, 236, 236, 255)
        else:
            img.alpha_composite(glow_rect((w, h), (3, 3, w - 6, h - 6), 16, L.NEON, 3, 3, 150))
            paste(img, vgrad(w - 12, h - 12, (64, 18, 34), (34, 10, 22)), rr_mask(w - 12, h - 12, (0, 0, w - 12, h - 12), 14), (6, 6))
            col = (236, 120, 132, 255)
        put_text(img, label, size, col, (w / 2.0, h / 2.0), tracking=0.03,
                 shadow=(60, 0, 10, 160) if on else None)
    return img


def make_controls():
    def pair(name, rect, label, style='seg', size=21):
        _, _, w, h = rect
        save(button(w, h, label, style, True, size), name + '_on.png')
        save(button(w, h, label, style, False, size), name + '_off.png')
    for i, lab in enumerate(L.REF_LABELS):
        pair('cal_%d' % i, L.CAL_BUTTONS[i], lab + ' dBFS', size=20)
    _, _, w, h = L.PEAK_BUTTON
    save(button(w, h, 'ON', 'seg', True, 24), 'peak_on.png')
    save(button(w, h, 'OFF', 'seg', False, 24), 'peak_off.png')
    for i, lab in enumerate(('WARM', 'COOL', 'OFF', 'REACT', 'CYCLE')):
        style = {'REACT': 'react', 'CYCLE': 'cycle'}.get(lab, 'seg')
        pair('light_%d' % i, L.LIGHT_BUTTONS[i], lab, style, size=24 if style != 'seg' else 19)
    pair('bypass', L.BYPASS_BUTTON, 'BYPASS', 'bypass', size=40)
    pair('auto', L.AUTO_BUTTON, 'AUTO', size=22)
    for i, lab in enumerate(L.CLIP_LABELS):
        pair('clip_%d' % i, L.CLIP_BUTTONS[i], lab, size=18)
    for i, lab in enumerate(L.RATE_LABELS):
        pair('rate_%d' % i, L.RATE_BUTTONS[i], lab, size=19)
    for i, (lab, name) in enumerate(zip(L.PALETTE_LABELS, L.PALETTE_ORDER)):
        pair('pal_%d' % i, L.PALETTE_BUTTONS[i], lab, 'pal:' + name, size=19)

    # AUTO "listening" halo: 2 frames (off / amber glow), drawn under the AUTO button
    ax, ay, aw, ah = L.AUTO_BUTTON
    pad = L.AUTO_GLOW_PAD
    gw, gh = aw + 2 * pad, ah + 2 * pad
    strip = Image.new('RGBA', (gw, gh * 2), (0, 0, 0, 0))
    halo = glow_rect((gw, gh), (pad - 1, pad - 1, aw + 2, ah + 2), 12, (255, 196, 90), 3.5, 6, 255)
    halo.alpha_composite(glow_rect((gw, gh), (pad - 1, pad - 1, aw + 2, ah + 2), 12, (255, 240, 200), 1, 2, 255))
    strip.alpha_composite(halo, (0, gh))
    save(strip, 'auto_busy.png')

    # phase correlation: PHASE caption + -1 .. +1 track with a glowing marker; frame 0 = no signal
    cx, cy, cw, ch = L.CORR
    frames = L.CORR_STEPS + 1
    strip = Image.new('RGBA', (cw, ch * frames), (0, 0, 0, 0))
    cap = text_image('PHASE', 15, L.CREAM + (255,), tracking=0.1, shadow=(0, 0, 0, 200))
    tx0, tx1, ty = 64, cw - 12, 15
    for f in range(frames):
        img = Image.new('RGBA', (cw * SS, ch * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([tx0 * SS, (ty - 5) * SS, tx1 * SS, (ty + 5) * SS], radius=5 * SS, fill=(18, 10, 8, 230),
                            outline=L.BRASS + (255,), width=int(1.2 * SS))
        mid = (tx0 + tx1) / 2.0
        d.rounded_rectangle([(tx0 + 3) * SS, (ty - 2) * SS, (mid - 2) * SS, (ty + 2) * SS], radius=2 * SS, fill=(150, 30, 40, 200))
        d.rounded_rectangle([(mid + 2) * SS, (ty - 2) * SS, (tx1 - 3) * SS, (ty + 2) * SS], radius=2 * SS, fill=(60, 120, 70, 200))
        for k, lab in ((0, '\u22121'), (0.5, '0'), (1, '+1')):
            x = tx0 + 4 + (tx1 - tx0 - 8) * k
            d.line([(x * SS, (ty + 6) * SS), (x * SS, (ty + 10) * SS)], fill=L.BRASS_LIGHT + (255,), width=int(1.2 * SS))
        img = img.resize((cw, ch), Image.LANCZOS)
        for k, lab in ((0, '\u22121'), (0.5, '0'), (1, '+1')):
            x = tx0 + 4 + (tx1 - tx0 - 8) * k
            t = text_image(lab, 11, L.CREAM + (230,), slant=0.0, semi=True)
            img.alpha_composite(t, (int(x - t.size[0] / 2), ty + 11))
        img.alpha_composite(cap, (2, ty - cap.size[1] // 2))
        if f > 0:
            r = (f - 1) / float(L.CORR_STEPS - 1)            # 0 = -1, 1 = +1
            x = tx0 + 4 + (tx1 - tx0 - 8) * r
            col = (255, 70, 80) if r < 0.45 else ((255, 200, 90) if r < 0.6 else (150, 255, 150))
            mk = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
            ImageDraw.Draw(mk).ellipse([x - 5, ty - 5, x + 5, ty + 5], fill=col + (255,))
            img.alpha_composite(mk.filter(ImageFilter.GaussianBlur(3)))
            img.alpha_composite(mk)
            ImageDraw.Draw(img).ellipse([x - 2, ty - 3, x + 1, ty], fill=(255, 255, 255, 200))
        strip.alpha_composite(img, (0, f * ch))
    save(strip, 'corr.png')

    # clip lamps: 2-frame strip, off / lit
    n = L.LAMP
    strip = Image.new('RGBA', (n, n * 2), (0, 0, 0, 0))
    for f, lit in enumerate((False, True)):
        lamp = Image.new('RGBA', (n * SS, n * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(lamp)
        d.ellipse([1 * SS, 1 * SS, (n - 1) * SS, (n - 1) * SS], fill=(30, 18, 14, 255))
        d.ellipse([4 * SS, 4 * SS, (n - 4) * SS, (n - 4) * SS], fill=(255, 60, 70, 255) if lit else (92, 20, 26, 255))
        d.ellipse([9 * SS, 7 * SS, 16 * SS, 13 * SS], fill=(255, 220, 220, 200) if lit else (200, 120, 120, 70))
        lamp = lamp.resize((n, n), Image.LANCZOS)
        if lit:
            g = lamp.filter(ImageFilter.GaussianBlur(3))
            lamp = Image.alpha_composite(g, lamp)
        strip.alpha_composite(lamp, (0, f * n))
    save(strip, 'lamp.png')
    clear = Image.new('RGBA', (L.LAMP_HIT_W, L.LAMP + 8), (0, 0, 0, 0))
    save(clear, 'hit_on.png')
    save(clear, 'hit_off.png')

    # reference plates
    _, _, pw, ph = L.PLATE
    for i, lab in enumerate(L.REF_LABELS):
        p = Image.new('RGBA', (pw, ph), (0, 0, 0, 0))
        paste(p, vgrad(pw, ph, L.BRASS_LIGHT, L.BRASS_DARK), rr_mask(pw, ph, (0, 0, pw, ph), ph // 2))
        paste(p, vgrad(pw - 6, ph - 6, (52, 26, 14), (28, 12, 8)), rr_mask(pw - 6, ph - 6, (0, 0, pw - 6, ph - 6), (ph - 6) // 2), (3, 3))
        d = ImageDraw.Draw(p)
        for sx in (18, pw - 18):
            d.ellipse([sx - 4, ph / 2 - 4, sx + 4, ph / 2 + 4], fill=L.BRASS_LIGHT)
        put_text(p, '0 VU = %s dBFS' % lab, 22, L.CREAM + (255,), (pw / 2.0, ph / 2.0), tracking=0.03)
        save(p, 'plate_%d.png' % i)

    # trim knob: 127-frame strip, -135..+135 degrees, amber arc from centre (bipolar)
    k = L.KNOB_SIZE
    frames = 127
    body = Image.new('RGBA', (k * SS, k * SS), (0, 0, 0, 0))
    bd = ImageDraw.Draw(body)
    c = k * SS / 2.0
    for i in range(40):
        t = i / 39.0
        r = (k * 0.36) * SS * (1 - t * 0.85)
        col = tuple(int(L.BRASS_DARK[j] + (L.BRASS_LIGHT[j] - L.BRASS_DARK[j]) * (0.25 + 0.75 * t)) for j in range(3))
        off = t * 5 * SS
        bd.ellipse([c - r - off, c - r - off, c + r - off, c + r - off], fill=col + (255,))
    bd.ellipse([c - k * 0.36 * SS, c - k * 0.36 * SS, c + k * 0.36 * SS, c + k * 0.36 * SS], outline=(60, 34, 12, 255), width=int(1.5 * SS))
    strip = Image.new('RGBA', (k, k * frames), (0, 0, 0, 0))
    for f in range(frames):
        v = f / (frames - 1.0)
        ang = -135 + 270 * v
        img = Image.new('RGBA', (k * SS, k * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        ro = k * 0.47 * SS
        d.arc([c - ro, c - ro, c + ro, c + ro], -225, 45, fill=(70, 50, 60, 255), width=int(3 * SS))
        a0, a1 = sorted((0.0, ang))
        if a1 - a0 > 0.5:
            d.arc([c - ro, c - ro, c + ro, c + ro], a0 - 90, a1 - 90, fill=L.AMBER + (255,), width=int(3.4 * SS))
        img.alpha_composite(body)
        a = math.radians(ang)
        r0, r1 = k * 0.12 * SS, k * 0.33 * SS
        d.line([(c + r0 * math.sin(a), c - r0 * math.cos(a)), (c + r1 * math.sin(a), c - r1 * math.cos(a))],
               fill=(250, 244, 230, 255), width=int(3 * SS))
        strip.alpha_composite(img.resize((k, k), Image.LANCZOS), (0, f * k))
    save(strip, 'knob_trim.png')


# ---------------------------------------------------------------------------- previews
def compose(bg, faces, mode, light_i, vus, cal=1, heat=0.0, clip=True, hue=0.0, palette=0, corr=0.9, flash=False):
    page = bg.copy()
    light = L.LIGHT_ORDER[light_i]
    if light in L.GLOW_LIGHTS:
        if flash:
            step = L.FLASH_FRAME
        elif light == 'react':
            step = palette * L.HEAT_FRAMES + int(round(heat * (L.HEAT_FRAMES - 1)))
        else:
            step = L.HUE_FIRST + int(round(hue * L.HUE_FRAMES)) % L.HUE_FRAMES
        halo = Image.open(os.path.join(OUT, 'glow_halo.png'))
        hw, hh = halo.size[0], halo.size[1] // L.GLOW_FRAMES
        page.alpha_composite(halo.crop((0, step * hh, hw, (step + 1) * hh)).resize(L.HALO[2:], Image.BILINEAR), L.HALO[:2])
        glow = Image.open(os.path.join(OUT, 'glow_%s.png' % mode))
        gw, gh = glow.size[0], glow.size[1] // L.GLOW_FRAMES
        page.alpha_composite(glow.crop((0, step * gh, gw, (step + 1) * gh)).resize(L.CABINET_INNER[2:], Image.BILINEAR),
                             L.CABINET_INNER[:2])
        light = 'glow'
    page.alpha_composite(faces[(mode, light)], L.CABINET_INNER[:2])
    def needle(kind, dial, vu):
        f = round(L.fraction(vu) * (dial.frames - 1))
        per = dial.frames // dial.sectors
        k = min(f // per, dial.sectors - 1)
        x0, y0, bw, bh = L.sector_box(dial, k)
        strip = Image.open(os.path.join(OUT, L.needle_file(kind, k)))
        fr = strip.crop((0, (f - k * per + 1) * bh, bw, (f - k * per + 2) * bh))
        page.alpha_composite(fr, (dial.window[0] + x0, dial.window[1] + y0))
    if mode == 'stereo':
        needle('s', L.stereo_dial(0), vus[0])
        needle('s', L.stereo_dial(1), vus[1])
        lamps = L.STEREO_LAMPS
    else:
        needle('m', L.mono_dial(), vus[0])
        lamps = [L.MONO_LAMP]
    lamp = Image.open(os.path.join(OUT, 'lamp.png'))
    for i, (x, y) in enumerate(lamps):
        lit = clip and (i == len(lamps) - 1) and mode == 'stereo'
        page.alpha_composite(lamp.crop((0, L.LAMP if lit else 0, L.LAMP, 2 * L.LAMP if lit else L.LAMP)), (x, y))
    page.alpha_composite(Image.open(os.path.join(OUT, 'plate_%d.png' % cal)), L.PLATE[:2])
    cs = Image.open(os.path.join(OUT, 'corr.png'))
    cf = 1 + int(round((corr + 1) / 2.0 * (L.CORR_STEPS - 1)))
    page.alpha_composite(cs.crop((0, cf * L.CORR[3], L.CORR[2], (cf + 1) * L.CORR[3])), L.CORR[:2])
    windows = [L.STEREO_WINDOWS[0], L.STEREO_WINDOWS[1]] if mode == 'stereo' else [L.MONO_WINDOW]
    for w, vu in zip(windows, [max(vus) + 0.8] * len(windows)):
        x, y, mw, mh = L.max_label_rect(w)
        t = text_image('MAX %+.1f VU' % vu, 17, L.INK + (255,), slant=0.0, semi=True)
        page.alpha_composite(t, (x + mw - t.size[0], y + (mh - t.size[1]) // 2))
    def btn(name, rect, on):
        img = Image.open(os.path.join(OUT, name + ('_on.png' if on else '_off.png')))
        x, y, w, h = rect
        page.alpha_composite(img, (x + (w - img.size[0]) // 2, y + (h - img.size[1]) // 2))
    btn('peak', L.PEAK_BUTTON, True)
    btn('bypass', L.BYPASS_BUTTON, False)
    btn('auto', L.AUTO_BUTTON, False)
    knob = Image.open(os.path.join(OUT, 'knob_trim.png'))
    page.alpha_composite(knob.crop((0, 63 * L.KNOB_SIZE, L.KNOB_SIZE, 64 * L.KNOB_SIZE)), L.KNOB[:2])
    tx, ty, tw, th = L.TRIM_VALUE
    put_text(page, '0.0 dB', 30, L.CREAM + (255,), (tx + tw / 2.0, ty + th / 2.0), slant=0.0, semi=True)
    screen = Image.new('RGBA', (L.W, 800), (34, 35, 44, 255))
    hdr = ImageDraw.Draw(screen)
    hdr.text((40, 34), 'MPC host header (not part of the skin)', fill=(150, 150, 160, 255), font=font(26, True))
    screen.alpha_composite(page, (0, L.SCREEN_TOP))
    return screen


def preview(bg, faces, mode, light_i, vus, cal=1, out=None, heat=0.0, hue=0.0, **kw):
    os.makedirs(L.PREVIEW_DIR, exist_ok=True)
    compose(bg, faces, mode, light_i, vus, cal, heat, hue=hue, **kw).convert('RGB').save(os.path.join(L.PREVIEW_DIR, out))


def setup_preview(sbg, clip=0, rate=1, pal=0, cal=1, light=3):
    page = sbg.copy()
    def btn(name, rect, on):
        img = Image.open(os.path.join(OUT, name + ('_on.png' if on else '_off.png')))
        page.alpha_composite(img, rect[:2])
    for i, r in enumerate(L.CAL_BUTTONS):
        btn('cal_%d' % i, r, i == cal)
    for i, r in enumerate(L.LIGHT_BUTTONS):
        btn('light_%d' % i, r, i == light)
    for i, r in enumerate(L.CLIP_BUTTONS):
        btn('clip_%d' % i, r, i == clip)
    for i, r in enumerate(L.RATE_BUTTONS):
        btn('rate_%d' % i, r, i == rate)
    for i, r in enumerate(L.PALETTE_BUTTONS):
        btn('pal_%d' % i, r, i == pal)
    screen = Image.new('RGBA', (L.W, 800), (34, 35, 44, 255))
    ImageDraw.Draw(screen).text((40, 34), 'MPC host header (not part of the skin)', fill=(150, 150, 160, 255), font=font(26, True))
    screen.alpha_composite(page, (0, L.SCREEN_TOP))
    screen.convert('RGB').save(os.path.join(L.PREVIEW_DIR, 'preview-setup.png'))


def cycle_gif(bg, faces):
    """Animated docs preview of Cycle: two bars at 120 BPM (4 s) = one trip round the hue wheel."""
    import math as m
    frames, n = [], 60
    for i in range(n):
        t = i / 15.0
        beat = (t * 2.0) % 1.0                                   # 120 BPM
        kick = m.exp(-beat * 5.0)
        vus = (-9 + 9 * kick + 1.5 * m.sin(i * 1.3), -10 + 8 * kick + 1.5 * m.sin(i * 1.9 + 1))
        img = compose(bg, faces, 'stereo', 4, vus, hue=i / float(n), clip=False).crop((0, L.SCREEN_TOP, L.W, L.SCREEN_TOP + 470))
        frames.append(img.convert('RGB').resize((640, 235), Image.LANCZOS).convert('P', palette=Image.ADAPTIVE, colors=192))
    frames[0].save(os.path.join(L.PREVIEW_DIR, 'cycle.gif'), save_all=True, append_images=frames[1:], duration=67, loop=0,
                   optimize=True)


def react_gif(bg, faces):
    """Animated docs preview: a swell from quiet to pinned and back, with the same heat
    ballistics as the plugin (fast warm-up, slow cool-down)."""
    import math as m
    frames, heat, dt = [], 0.0, 1 / 15.0
    anchors = [(-20, 0.0), (-10, 0.22), (-5, 0.42), (0, 0.66), (2, 0.86), (3, 1.0)]
    def heat_for(vu):
        if vu <= anchors[0][0]: return 0.0
        for (a, ha), (b, hb) in zip(anchors, anchors[1:]):
            if vu <= b: return ha + (vu - a) / (b - a) * (hb - ha)
        return 1.0
    n = 60
    for i in range(n):
        ph = i / float(n)
        level = -24 + 27.5 * (0.5 - 0.5 * m.cos(2 * m.pi * ph)) ** 1.6
        vus = (level + 0.6 * m.sin(i * 1.7), level - 1.2 + 0.8 * m.sin(i * 2.3 + 1))
        target = heat_for(max(vus))
        tau = 0.12 if target > heat else 0.9
        heat += (target - heat) * (1 - m.exp(-dt / tau))
        img = compose(bg, faces, 'stereo', 3, vus, heat=heat, clip=max(vus) > 2.5).crop((0, L.SCREEN_TOP, L.W, L.SCREEN_TOP + 470))
        frames.append(img.convert('RGB').resize((640, 235), Image.LANCZOS).convert('P', palette=Image.ADAPTIVE, colors=192))
    frames[0].save(os.path.join(L.PREVIEW_DIR, 'react.gif'), save_all=True, append_images=frames[1:], duration=67, loop=0,
                   optimize=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    print('background'); bg = make_background(); save(bg, 'bg.png')
    sbg = make_setup_background(); save(sbg, 'bg_setup.png')
    faces = {}
    for mode in ['stereo']:
        for light in L.STATIC_LIGHTS + ['glow']:     # 'glow' = window overlay above the React/Cycle glow strip
            print('face', mode, light)
            faces[(mode, light)] = make_face(mode, light)
            save(faces[(mode, light)], 'face_%s_%s.png' % (mode, light))
    print('needles'); make_needles('s', L.stereo_dial(0))
    print('controls'); make_controls()
    print('glow strips'); make_glow()
    preview(bg, faces, 'stereo', 0, (0.0, -3.0), out='preview-stereo.png')
    preview(bg, faces, 'stereo', 1, (-7.0, 1.0), cal=0, out='preview-stereo-cool.png')
    preview(bg, faces, 'stereo', 2, (-20.0, -15.0), cal=2, out='preview-stereo-off.png')
    for name, heat, vus, mode in (('cold', 0.05, (-16.0, -18.0), 'stereo'), ('warm', 0.66, (0.0, -1.0), 'stereo'),
                                  ('hot', 1.0, (3.0, 2.6), 'stereo')):
        preview(bg, faces, mode, 3, vus, out='preview-react-%s.png' % name, heat=heat)
    for name, hue in (('red', 0.0), ('green', 1 / 3.0), ('blue', 2 / 3.0)):
        preview(bg, faces, 'stereo', 4, (-4.0, -6.0), out='preview-cycle-%s.png' % name, hue=hue)
    preview(bg, faces, 'stereo', 3, (2.0, 1.0), out='preview-react-neon.png', heat=0.8, palette=1, corr=0.2)
    preview(bg, faces, 'stereo', 3, (-2.0, -3.0), out='preview-react-amber.png', heat=0.55, palette=2, corr=0.7)
    preview(bg, faces, 'stereo', 4, (3.0, 2.0), out='preview-flash.png', flash=True, corr=-0.6)
    setup_preview(sbg)
    print('react gif'); react_gif(bg, faces)
    print('cycle gif'); cycle_gif(bg, faces)
    print('decoded needle strips: %.1f MiB' % (L.decoded_needle_bytes() / 1048576.0))


if __name__ == '__main__':
    main()
