# Original mark v3: a bare clenched fist (front view, knuckles toward the viewer), painted with layered
# shading, wrapped in blue electricity. No reference art used; all shapes are drawn here.
import math, random
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageChops

S = 2048
U = 120.0
CX, CY = S / 2, S * 0.47

def mask(draw_fn):
    m = Image.new("L", (S, S), 0)
    draw_fn(ImageDraw.Draw(m))
    return m

def arr(m):
    return np.asarray(m, dtype=np.float32) / 255.0

def blur(a, r):
    im = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))
    return arr(im.filter(ImageFilter.GaussianBlur(r)))

SKIN = np.array([206, 150, 118], np.float32)
SKIN_DARK = np.array([120, 72, 54], np.float32)
SKIN_LIGHT = np.array([246, 206, 178], np.float32)

def shade_part(m, round_r=60, light=(-1, -1.3), spec=0.35):
    """A skin-coloured, rounded-looking part from its mask: darker toward its edges, lit from the top left."""
    a = arr(m)
    b = blur(a, round_r)
    body = np.clip((b - 0.35) / 0.65, 0, 1)            # 0 at the rim, 1 inside
    lx, ly = light
    dx, dy = int(lx * 22), int(ly * 22)
    shifted = np.roll(np.roll(b, dy, 0), dx, 1)
    facing = np.clip((b - shifted) * 3.0, -1, 1)          # + where the surface faces the light
    tone = 0.62 + 0.38 * body + 0.35 * facing
    col = SKIN[None, None, :] * tone[..., None]
    # cool shadow at the rim, warm light on the facing side
    col = col * (1 - 0.35 * (1 - body))[..., None] + SKIN_DARK[None, None, :] * (0.35 * (1 - body))[..., None]
    hi = np.clip(facing, 0, 1) ** 2 * spec
    col = col * (1 - hi[..., None]) + SKIN_LIGHT[None, None, :] * hi[..., None]
    rgba = np.zeros((S, S, 4), np.float32)
    rgba[..., :3] = np.clip(col, 0, 255)
    rgba[..., 3] = a * 255
    return Image.fromarray(rgba.astype(np.uint8), "RGBA")

def contact_shadow(m, r=28, strength=0.55, offset=(0, 10)):
    a = blur(arr(m), r)
    a = np.roll(np.roll(a, offset[1], 0), offset[0], 1)
    rgba = np.zeros((S, S, 4), np.float32)
    rgba[..., :3] = (40, 20, 16)
    rgba[..., 3] = np.clip(a * strength, 0, 1) * 255
    return Image.fromarray(rgba.astype(np.uint8), "RGBA")

def crease(draw_fn, r=6, alpha=150):
    m = mask(draw_fn).filter(ImageFilter.GaussianBlur(r))
    a = arr(m)
    rgba = np.zeros((S, S, 4), np.float32)
    rgba[..., :3] = (92, 50, 38)
    rgba[..., 3] = a * alpha
    return Image.fromarray(rgba.astype(np.uint8), "RGBA")

def paint_fist():
    layers = []
    # wrist and forearm, slightly narrower, going down
    wrist = mask(lambda d: d.rounded_rectangle((CX - 2.6*U, CY + 3.4*U, CX + 2.4*U, CY + 9*U), radius=1.2*U, fill=255))
    layers.append(shade_part(wrist, 90))
    # back of the hand behind the fingers
    hand = mask(lambda d: d.rounded_rectangle((CX - 3.9*U, CY - 1.4*U, CX + 3.6*U, CY + 4.4*U), radius=1.7*U, fill=255))
    layers.append(contact_shadow(hand, 40, 0.5, (0, 30)))
    layers.append(shade_part(hand, 110))
    # four fingers: the curled middle sections facing us, each a rounded block; middle two stand higher
    tops = [-2.2, -2.8, -2.6, -1.9]
    widths = [1.95, 2.05, 1.95, 1.75]
    x = CX - 4.05*U
    fingers = []
    for t, w in zip(tops, widths):
        fingers.append((x, t, w))
        x += w * U - 0.06*U
    for i, (x0, t, w) in enumerate(fingers):
        f = mask(lambda d, x0=x0, t=t, w=w: d.rounded_rectangle((x0, CY + t*U, x0 + w*U, CY + 2.2*U), radius=0.92*U, fill=255))
        layers.append(contact_shadow(f, 22, 0.6, (6, 14)))
        layers.append(shade_part(f, 52))
        # the knuckle bump on top and the middle joint crease
        layers.append(crease(lambda d, x0=x0, t=t, w=w: d.arc((x0 + 0.3*U, CY + (t + 1.2)*U, x0 + (w - 0.3)*U, CY + (t + 2.2)*U), 200, 340, fill=255, width=10), 5, 120))
        layers.append(crease(lambda d, x0=x0, t=t, w=w: d.line((x0 + 0.42*U, CY + (t + 2.55)*U, x0 + (w - 0.42)*U, CY + (t + 2.45)*U), fill=255, width=9), 4, 110))
    # the gaps between the fingers
    for (x0, t, w) in fingers[1:]:
        layers.append(crease(lambda d, x0=x0, t=t: d.line((x0 + 0.02*U, CY + (t + 0.9)*U, x0 + 0.02*U, CY + 2.0*U), fill=255, width=12), 6, 170))
    # the thumb across the fingers, from the left, its tip near the middle finger
    thumb = mask(lambda d: d.rounded_rectangle((CX - 3.9*U, CY + 0.55*U, CX + 1.75*U, CY + 2.65*U), radius=1.05*U, fill=255))
    layers.append(contact_shadow(thumb, 26, 0.7, (4, -14)))
    layers.append(contact_shadow(thumb, 30, 0.5, (0, 22)))
    layers.append(shade_part(thumb, 58, spec=0.45))
    # thumbnail at the tip
    nail = mask(lambda d: d.rounded_rectangle((CX + 0.55*U, CY + 0.85*U, CX + 1.5*U, CY + 1.95*U), radius=0.42*U, fill=255))
    nrgba = np.zeros((S, S, 4), np.float32)
    na = blur(arr(nail), 3)
    nrgba[..., :3] = (232, 196, 180)
    nrgba[..., 3] = na * 200
    layers.append(Image.fromarray(nrgba.astype(np.uint8), "RGBA"))
    layers.append(crease(lambda d: d.line((CX - 1.0*U, CY + 1.0*U, CX - 0.9*U, CY + 2.3*U), fill=255, width=8), 4, 90))
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    for l in layers:
        out = Image.alpha_composite(out, l)
    # fine skin grain
    rng = np.random.default_rng(3)
    a = np.asarray(out, np.float32)
    noise = rng.normal(0, 4.0, (S, S, 1)).astype(np.float32)
    a[..., :3] = np.clip(a[..., :3] + noise * (a[..., 3:4] / 255), 0, 255)
    return Image.fromarray(a.astype(np.uint8), "RGBA")

# ---------------------------------------------------------------- electricity
def bolt_pts(rnd, start, angle, length, n=12, jag=0.7):
    x, y = start
    pts = [(x, y)]
    for _ in range(n):
        a = angle + rnd.uniform(-jag, jag)
        x += math.cos(a) * length / n
        y += math.sin(a) * length / n
        pts.append((x, y))
    return pts

def electricity(seed):
    rnd = random.Random(seed)
    bolts = []
    for k in range(9):
        ang = -math.pi / 2 + (k - 4) * 0.42 + rnd.uniform(-0.08, 0.08)
        r = 4.6 * U
        start = (CX + math.cos(ang) * r * 0.95, CY - 0.2*U + math.sin(ang) * r * 0.8)
        main = bolt_pts(rnd, start, ang, rnd.uniform(2.6, 4.4) * U)
        bolts.append((main, 1.0))
        for j in (4, 8):
            if rnd.random() < 0.65:
                bolts.append((bolt_pts(rnd, main[j], ang + rnd.choice([-1, 1]) * rnd.uniform(0.6, 1.0), rnd.uniform(0.9, 1.7) * U, 7), 0.5))
    # arcs crawling over the knuckles and around the hand
    for k in range(4):
        a0 = rnd.uniform(-2.6, -0.4)
        pts = []
        for i in range(16):
            a = a0 + i * 0.13
            rad = (4.3 + rnd.uniform(-0.25, 0.25)) * U
            pts.append((CX + math.cos(a) * rad, CY + 0.3*U + math.sin(a) * rad * 0.9))
        bolts.append((pts, 0.6))
    for k in range(3):  # small sparks on the knuckles
        x0 = CX - 2.6*U + k * 2.2*U
        bolts.append((bolt_pts(rnd, (x0, CY - 2.2*U), rnd.uniform(-0.4, 0.4), 1.6*U, 8, 1.0), 0.45))
    return bolts

def draw_bolts(bolts, color, width):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for pts, k in bolts:
        d.line(pts, fill=color, width=max(2, int(width * k)), joint="curve")
    return img

BLUE_DEEP, BLUE, CYAN, WHITE = (20, 60, 255, 255), (40, 140, 255, 255), (120, 210, 255, 255), (235, 248, 255, 255)

def make(path, size, tile, heavy=False):
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    if tile:
        ImageDraw.Draw(base).rounded_rectangle((40, 40, S - 40, S - 40), radius=380, fill=(8, 10, 18, 255))
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((CX - 6.5*U, CY - 6*U, CX + 6.5*U, CY + 6*U), fill=(30, 90, 255, 110))
    base = Image.alpha_composite(base, glow.filter(ImageFilter.GaussianBlur(170)))
    bolts = electricity(5)
    w = 1.8 if heavy else 1.0
    base = Image.alpha_composite(base, draw_bolts(bolts, BLUE_DEEP, int(110 * w)).filter(ImageFilter.GaussianBlur(70)))
    base = Image.alpha_composite(base, draw_bolts(bolts, BLUE, int(46 * w)).filter(ImageFilter.GaussianBlur(22)))
    fist = paint_fist()
    # blue rim light on the fist from the electricity
    rim = Image.new("RGBA", (S, S), (80, 170, 255, 0))
    rim.putalpha(fist.split()[3].filter(ImageFilter.GaussianBlur(26)).point(lambda v: int(v * 0.8)))
    base = Image.alpha_composite(base, rim)
    base = Image.alpha_composite(base, fist)
    edge = ImageChops.subtract(fist.split()[3], fist.split()[3].filter(ImageFilter.GaussianBlur(10)))
    rim2 = Image.new("RGBA", (S, S), (140, 210, 255, 0))
    rim2.putalpha(edge.point(lambda v: min(255, v * 3)))
    base = Image.alpha_composite(base, rim2)
    base = Image.alpha_composite(base, draw_bolts(bolts, CYAN, int(22 * w)))
    base = Image.alpha_composite(base, draw_bolts(bolts, WHITE, int(8 * w)))
    if tile:
        m = Image.new("L", (S, S), 0)
        ImageDraw.Draw(m).rounded_rectangle((40, 40, S - 40, S - 40), radius=380, fill=255)
        base.putalpha(ImageChops.multiply(base.split()[3], m))
    base.resize((size, size), Image.LANCZOS).save(path)

if __name__ == "__main__":
    make("fist3_tile_512.png", 512, True)
    print("ok")
