"""Generate the native app's artwork: smooth UI fonts, icons, logo tile, app icon.

Outputs (under build/native, the native build's -I include dir):
  ui_font.h  - anti-aliased glyph bitmaps at several native pixel sizes
  ui_icons.h - 8-bit alpha masks (tinted at runtime) + the logo tile (ARGB)
  icon0.png  - 512x512 PS4 home-screen icon

The same data is exposed through build_data() so the PC previewer renders
exactly what the console will.

Font note: glyph bitmaps are baked from DejaVu Sans (free license, see
LICENSE_DEJAVU.txt), taken from the copy bundled with matplotlib.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

root = Path(__file__).resolve().parent
out = root.parent / 'build/native'

FONT_REG = 'DejaVuSans.ttf'
FONT_SEMI = 'DejaVuSans-Bold.ttf'

# name, file, pixel size. Sizes are native: nothing is ever pixel-doubled.
FACES = [
    ('TITLE', FONT_SEMI, 40),
    ('NAME', FONT_SEMI, 27),
    ('BODY', FONT_REG, 23),
    ('SMALL', FONT_REG, 20),
    ('SMALLB', FONT_SEMI, 20),
]
GAMMA = 0.80  # <1 thickens light-on-dark text slightly so it stays crisp on TVs

MINT = (133, 228, 192, 255)
WHITE = (240, 246, 252, 255)
SS = 8  # supersampling for icon masks


def _font(name, size):
    import importlib.util
    spec = importlib.util.find_spec('matplotlib')
    if spec is None or not spec.origin:
        raise SystemExit('matplotlib is required: it supplies the DejaVu Sans font files.')
    p = Path(spec.origin).parent / 'mpl-data/fonts/ttf' / name
    if not p.exists():
        raise SystemExit(f'DejaVu font not found: {p}')
    return ImageFont.truetype(str(p), size)


# --- Fonts -------------------------------------------------------------------
def build_face(path, size):
    font = _font(path, size)
    asc, desc = font.getmetrics()
    lut = [min(255, round(255 * ((i / 255.0) ** GAMMA))) for i in range(256)]
    glyphs, bits = [], bytearray()
    for code in range(32, 127):
        ch = chr(code)
        adv16 = round(font.getlength(ch) * 16)
        x0, y0, x1, y1 = font.getbbox(ch, anchor='ls')
        w, h = x1 - x0, y1 - y0
        if w <= 0 or h <= 0 or ch == ' ':
            glyphs.append((len(bits), adv16, 0, 0, 0, 0))
            continue
        im = Image.new('L', (w, h), 0)
        ImageDraw.Draw(im).text((-x0, -y0), ch, font=font, fill=255, anchor='ls')
        off = len(bits)
        bits.extend(lut[v] for v in im.tobytes())
        glyphs.append((off, adv16, w, h, x0, y0))
    return dict(glyphs=glyphs, bits=bytes(bits), asc=asc, line=asc + desc)


# --- Icons (alpha masks) ------------------------------------------------------
def mask_canvas(w, h):
    return Image.new('L', (w * SS, h * SS), 0)


def finish(im, w, h):
    return im.resize((w, h), Image.LANCZOS)


def draw_headphones(d, cx, cy, s, fill=255):
    """Band + two earcups; same motif as the app icon, without the rune."""
    band_w = max(2, int(0.17 * s))
    d.arc((cx - 0.74 * s, cy - 0.72 * s, cx + 0.74 * s, cy + 0.40 * s), 180, 360, fill=fill, width=band_w)
    cup_w, cup_h = 0.27 * s, 0.46 * s
    top = cy - 0.18 * s
    rad = int(cup_w * 0.45)
    d.rounded_rectangle((cx - 0.74 * s, top, cx - 0.74 * s + cup_w, top + cup_h), radius=rad, fill=fill)
    d.rounded_rectangle((cx + 0.74 * s - cup_w, top, cx + 0.74 * s, top + cup_h), radius=rad, fill=fill)


def icon_headphones(size):
    im = mask_canvas(size, size)
    draw_headphones(ImageDraw.Draw(im), size * SS * 0.5, size * SS * 0.50, size * SS * 0.60)
    return finish(im, size, size)


def icon_check(size):
    im = mask_canvas(size, size)
    d = ImageDraw.Draw(im)
    w = int(size * SS * 0.13)
    pts = [(0.27, 0.53), (0.44, 0.70), (0.75, 0.33)]
    pts = [(x * size * SS, y * size * SS) for x, y in pts]
    d.line(pts, fill=255, width=w, joint='curve')
    for x, y in (pts[0], pts[2]):
        d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=255)
    return finish(im, size, size)


def icon_bang(size):
    im = mask_canvas(size, size)
    d = ImageDraw.Draw(im)
    S = size * SS
    w = 0.15 * S
    d.rounded_rectangle((S / 2 - w / 2, 0.20 * S, S / 2 + w / 2, 0.60 * S), radius=int(w / 2), fill=255)
    d.ellipse((S / 2 - w / 2, 0.70 * S, S / 2 + w / 2, 0.70 * S + w), fill=255)
    return finish(im, size, size)


def icon_cross_mark(size):
    im = mask_canvas(size, size)
    d = ImageDraw.Draw(im)
    S = size * SS
    w = int(0.13 * S)
    for a, b in (((0.28, 0.28), (0.72, 0.72)), ((0.72, 0.28), (0.28, 0.72))):
        d.line([(a[0] * S, a[1] * S), (b[0] * S, b[1] * S)], fill=255, width=w)
        for x, y in (a, b):
            d.ellipse((x * S - w / 2, y * S - w / 2, x * S + w / 2, y * S + w / 2), fill=255)
    return finish(im, size, size)


def icon_speaker(size):
    im = mask_canvas(size, size)
    d = ImageDraw.Draw(im)
    S = size * SS
    d.polygon([(0.10 * S, 0.38 * S), (0.30 * S, 0.38 * S), (0.52 * S, 0.18 * S), (0.52 * S, 0.82 * S),
               (0.30 * S, 0.62 * S), (0.10 * S, 0.62 * S)], fill=255)
    w = int(0.075 * S)
    d.arc((0.40 * S, 0.30 * S, 0.76 * S, 0.70 * S), -50, 50, fill=255, width=w)
    d.arc((0.40 * S, 0.14 * S, 0.94 * S, 0.86 * S), -50, 50, fill=255, width=w)
    return finish(im, size, size)


def icon_glow(size):
    """Soft radial falloff used as a state-coloured halo."""
    im = Image.new('L', (size, size), 0)
    px = im.load()
    c = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            r = ((x - c) ** 2 + (y - c) ** 2) ** 0.5 / c
            if r < 1:
                t = 1 - r
                px[x, y] = int(255 * t * t * (3 - 2 * t))
    return im


def btn_cross(size):
    return icon_cross_mark_thin(size)


def icon_cross_mark_thin(size):
    im = mask_canvas(size, size)
    d = ImageDraw.Draw(im)
    S = size * SS
    w = int(0.13 * S)
    for a, b in (((0.24, 0.24), (0.76, 0.76)), ((0.76, 0.24), (0.24, 0.76))):
        d.line([(a[0] * S, a[1] * S), (b[0] * S, b[1] * S)], fill=255, width=w)
        for x, y in (a, b):
            d.ellipse((x * S - w / 2, y * S - w / 2, x * S + w / 2, y * S + w / 2), fill=255)
    return finish(im, size, size)


def btn_circle(size):
    im = mask_canvas(size, size)
    S = size * SS
    ImageDraw.Draw(im).ellipse((0.20 * S, 0.20 * S, 0.80 * S, 0.80 * S), outline=255, width=int(0.12 * S))
    return finish(im, size, size)


def btn_square(size):
    im = mask_canvas(size, size)
    S = size * SS
    ImageDraw.Draw(im).rounded_rectangle((0.22 * S, 0.22 * S, 0.78 * S, 0.78 * S), radius=int(0.06 * S),
                                         outline=255, width=int(0.12 * S))
    return finish(im, size, size)


def btn_triangle(size):
    im = mask_canvas(size, size)
    d = ImageDraw.Draw(im)
    S = size * SS
    pts = [(0.50 * S, 0.17 * S), (0.84 * S, 0.77 * S), (0.16 * S, 0.77 * S)]
    w = int(0.12 * S)
    d.line(pts + [pts[0], pts[1]], fill=255, width=w, joint='curve')
    for x, y in pts:
        d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=255)
    return finish(im, size, size)


ICON_SPECS = [
    ('HERO', lambda: icon_headphones(88)),
    ('ROWHP', lambda: icon_headphones(30)),
    ('GLOW', lambda: icon_glow(230)),
    ('CHECK', lambda: icon_check(26)),
    ('BANG', lambda: icon_bang(26)),
    ('XMARK', lambda: icon_cross_mark(26)),
    ('SPEAKER', lambda: icon_speaker(26)),
    ('BCROSS', lambda: btn_cross(22)),
    ('BCIRCLE', lambda: btn_circle(22)),
    ('BSQUARE', lambda: btn_square(22)),
    ('BTRI', lambda: btn_triangle(22)),
]


# --- Shared motif for the app icon / logo tile ---------------------------------
def draw_bt(d, cx, cy, hw, hh, color, width):
    unit = [(-1, -0.5), (1, 0.5), (0, 1), (0, -1), (1, -0.5), (-1, 0.5)]
    pts = [(cx + px * hw, cy + py * hh) for px, py in unit]
    d.line(pts, fill=color, width=width, joint='curve')
    r = width / 2
    for px, py in pts:
        d.ellipse((px - r, py - r, px + r, py + r), fill=color)


def draw_motif(img, cx, cy, scale, head_color, bt_color):
    d = ImageDraw.Draw(img)
    draw_headphones(d, cx, cy, scale, fill=head_color)
    draw_bt(d, cx, cy + 0.10 * scale, 0.17 * scale, 0.32 * scale, bt_color, max(2, int(0.075 * scale)))


def build_icon512():
    S = 512 * 3
    top_c, bot_c = (0x12, 0x1b, 0x2c), (0x20, 0x30, 0x48)
    grad = Image.new('RGB', (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        gd.line([(0, y), (S, y)], fill=tuple(int(top_c[i] * (1 - t) + bot_c[i] * t) for i in range(3)))
    icon = grad.convert('RGBA')
    glow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((S * 0.26, S * 0.20, S * 0.74, S * 0.66), fill=(133, 228, 192, 70))
    icon = Image.alpha_composite(icon, glow.filter(ImageFilter.GaussianBlur(S * 0.035)))
    draw_motif(icon, S * 0.5, S * 0.46, S * 0.33, MINT, WHITE)
    ImageDraw.Draw(icon).rounded_rectangle((int(S * 0.045), int(S * 0.045), int(S * 0.955), int(S * 0.955)),
                                           radius=int(S * 0.11), outline=(133, 228, 192, 60), width=max(1, int(S * 0.006)))
    return icon.resize((512, 512), Image.LANCZOS).convert('RGB')


def build_logo_tile(icon, size=72, radius=16):
    tile = icon.resize((size, size), Image.LANCZOS).convert('RGBA')
    m = Image.new('L', (size * SS, size * SS), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size * SS - 1, size * SS - 1), radius=radius * SS, fill=255)
    tile.putalpha(m.resize((size, size), Image.LANCZOS))
    return tile


def build_data():
    icon = build_icon512()
    faces = {name: build_face(path, size) for name, path, size in FACES}
    icons = {name: fn() for name, fn in ICON_SPECS}
    return dict(faces=faces, icons=icons, icon512=icon, logo=build_logo_tile(icon))


# --- C header emission ---------------------------------------------------------
def write_headers(data):
    out.mkdir(parents=True, exist_ok=True)
    lines = ['/* Generated by native/make_assets.py - do not edit. */',
             'struct glyph{unsigned off;unsigned short adv16;unsigned char w,h;signed char bx,by;};',
             'struct face{const struct glyph*g;const unsigned char*bits;int asc,line;};']
    for name, _, _ in FACES:
        f = data['faces'][name]
        lines.append(f'static const unsigned char fb_{name}[]={{' + ','.join(map(str, f['bits'])) + '};')
        rows = ','.join('{%d,%d,%d,%d,%d,%d}' % g for g in f['glyphs'])
        lines.append(f'static const struct glyph fg_{name}[95]={{{rows}}};')
    lines.append('enum{' + ','.join(f'F_{n}' for n, _, _ in FACES) + ',F_COUNT};')
    lines.append('static const struct face FACES[F_COUNT]={' + ','.join(
        '{fg_%s,fb_%s,%d,%d}' % (n, n, data['faces'][n]['asc'], data['faces'][n]['line']) for n, _, _ in FACES) + '};')
    (out / 'ui_font.h').write_text('\n'.join(lines) + '\n')

    lines = ['/* Generated by native/make_assets.py - do not edit. */',
             'struct icon{const unsigned char*a;unsigned short w,h;};']
    for name, im in data['icons'].items():
        lines.append(f'static const unsigned char ia_{name}[]={{' + ','.join(map(str, im.tobytes())) + '};')
    lines.append('enum{' + ','.join(f'I_{n}' for n in data['icons']) + ',I_COUNT};')
    lines.append('static const struct icon ICONS[I_COUNT]={' + ','.join(
        '{ia_%s,%d,%d}' % (n, im.width, im.height) for n, im in data['icons'].items()) + '};')
    logo = data['logo']
    vals = [(a << 24) | (r << 16) | (g << 8) | b for (r, g, b, a) in logo.getdata()]
    lines.append(f'#define LOGO_W {logo.width}\n#define LOGO_H {logo.height}')
    lines.append('static const unsigned int logo_argb[LOGO_W*LOGO_H]={' + ','.join(map(str, vals)) + '};')
    (out / 'ui_icons.h').write_text('\n'.join(lines) + '\n')
    data['icon512'].save(out / 'icon0.png')


if __name__ == '__main__':
    d = build_data()
    write_headers(d)
    print('Native assets generated:', out)
    for n, _, _ in FACES:
        print('  face', n, len(d['faces'][n]['bits']), 'bytes')
    print('  icons', len(d['icons']), ' logo', d['logo'].size)
