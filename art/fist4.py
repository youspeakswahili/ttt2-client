# v4: v3's fist with harder contrast, blue light on the skin and the forearm fading out.
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageChops
import fist3 as f
S, U, CX, CY = f.S, f.U, f.CX, f.CY

def graded_fist():
    fist = f.paint_fist()
    a = np.asarray(fist, np.float32)
    rgb, al = a[..., :3], a[..., 3:4] / 255
    # contrast around the mid tone, a little darker overall
    rgb = np.clip((rgb - 128) * 1.28 + 112, 0, 255)
    # blue light from above and the sides: stronger near the outline, weaker inside
    inner = f.blur(al[..., 0], 70)[..., None]
    edge = np.clip(1 - (inner - 0.5) * 2, 0, 1)
    blue = np.array([70, 150, 255], np.float32)
    rgb = rgb * (1 - 0.42 * edge) + blue * 0.42 * edge
    # the forearm fades into the dark below the hand
    ys = np.arange(S, dtype=np.float32)[:, None, None]
    fade = np.clip(1 - (ys - (CY + 4.6 * U)) / (3.2 * U), 0, 1)
    al = al * fade
    rgb = rgb * (0.45 + 0.55 * fade)
    out = np.concatenate([rgb, al * 255], axis=2)
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGBA")

def make(path, size, tile, heavy=False):
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    if tile:
        ImageDraw.Draw(base).rounded_rectangle((40, 40, S - 40, S - 40), radius=380, fill=(6, 8, 16, 255))
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((CX - 6.5*U, CY - 6.5*U, CX + 6.5*U, CY + 5*U), fill=(30, 90, 255, 130))
    base = Image.alpha_composite(base, glow.filter(ImageFilter.GaussianBlur(170)))
    bolts = f.electricity(5)
    w = 1.9 if heavy else 1.15
    base = Image.alpha_composite(base, f.draw_bolts(bolts, f.BLUE_DEEP, int(120 * w)).filter(ImageFilter.GaussianBlur(70)))
    base = Image.alpha_composite(base, f.draw_bolts(bolts, f.BLUE, int(50 * w)).filter(ImageFilter.GaussianBlur(22)))
    fist = graded_fist()
    rim = Image.new("RGBA", (S, S), (80, 170, 255, 0))
    rim.putalpha(fist.split()[3].filter(ImageFilter.GaussianBlur(30)).point(lambda v: int(v * 0.85)))
    base = Image.alpha_composite(base, rim)
    base = Image.alpha_composite(base, fist)
    edge = ImageChops.subtract(fist.split()[3], fist.split()[3].filter(ImageFilter.GaussianBlur(10)))
    rim2 = Image.new("RGBA", (S, S), (150, 215, 255, 0))
    rim2.putalpha(edge.point(lambda v: min(255, v * 3)))
    base = Image.alpha_composite(base, rim2)
    base = Image.alpha_composite(base, f.draw_bolts(bolts, f.CYAN, int(22 * w)))
    base = Image.alpha_composite(base, f.draw_bolts(bolts, f.WHITE, int(8 * w)))
    if tile:
        m = Image.new("L", (S, S), 0)
        ImageDraw.Draw(m).rounded_rectangle((40, 40, S - 40, S - 40), radius=380, fill=255)
        base.putalpha(ImageChops.multiply(base.split()[3], m))
    base.resize((size, size), Image.LANCZOS).save(path)

make("fist4_tile_512.png", 512, True)
make("fist4_160.png", 160, False, heavy=True)
print("ok")
