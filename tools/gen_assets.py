#!/usr/bin/env python3
"""Generate avocatOS UI assets as LVGL 9 C sources.

    .venv/bin/python avocatos/tools/gen_assets.py

Outputs (committed, so builds never need Python/Node):
  components/avo_ui/assets/fonts/avo_font_*.c   Inter + LVGL symbols (lv_font_conv)
  components/avo_ui/assets/img/avo_img_*.c      avocado marks (RGB565A8), Flux digit masks (A8)
"""
from __future__ import annotations

import math
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT_SRC = ROOT / "assets" / "fonts"
OUT_FONTS = ROOT / "components" / "avo_ui" / "assets" / "fonts"
OUT_IMG = ROOT / "components" / "avo_ui" / "assets" / "img"

LV_FONT_CONV = ["npx", "-y", "lv_font_conv@1.5.2"]
# Latin-1 + typographic punctuation (– — ‘ ’ “ ” • … ‹ ›), €, ™, arrows
TEXT_RANGE = "0x20-0x7E,0xA0-0xFF,0x2010-0x2027,0x2030-0x203A,0x20AC,0x2122,0x2190-0x2193"
DIGIT_RANGE = "0x20,0x2C,0x2E,0x30-0x3A"
# LVGL built-in symbol code points (LV_SYMBOL_*)
SYMBOLS = (
    "61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,"
    "61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,"
    "61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,"
    "61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,"
    "62020,62087,62099,62189,62212,62810,63426,63650,"
    "61463,62194,62034,61675,61830,61829,61774,63166,62804,61948,61530,62171,61612,61548,61926,61475,62413,62153,63024,61942,61458,61524,61452,62790,61444"
)

# Single-colour emoji (Noto Emoji, OFL) used as fallback of the text fonts.
EMOJI_TTF = "noto-emoji-400.ttf"
EMOJI_RANGE = ("0x2190-0x21FF,0x2300-0x23FF,0x2460-0x24FF,0x25A0-0x27BF,0x2900-0x297F,0x2B00-0x2BFF,"
               "0x1F100-0x1F1FF,0x1F300-0x1F64F,0x1F680-0x1F6FF,0x1F900-0x1F9FF,0x1FA70-0x1FAFF")
EMOJI_SIZES = (22, 26, 30, 40)

# name, ttf, px, range, with symbols
FONTS = [
    ("avo_font_22", "inter-400.ttf", 22, TEXT_RANGE, True),
    ("avo_font_26", "inter-400.ttf", 26, TEXT_RANGE, True),
    ("avo_font_30", "inter-600.ttf", 30, TEXT_RANGE, True),
    ("avo_font_40", "inter-700.ttf", 40, TEXT_RANGE, True),
    ("avo_font_digits_76", "inter-700.ttf", 76, DIGIT_RANGE, False),
]

# ---------------------------------------------------------------- palette
SKIN = (0x2B, 0x46, 0x1C)
RIND = (0x7C, 0xB3, 0x42)
FLESH = (0xE4, 0xEE, 0xAB)
PIT = (0x7A, 0x4A, 0x29)
PIT_HI = (0x9B, 0x66, 0x3F)

SS = 4  # supersampling factor for anti-aliasing


def rel(path: Path) -> str:
    """Repo-relative path, so generated headers do not embed local paths."""
    return str(path.relative_to(ROOT))


def link_fallback(path: Path, px: int) -> None:
    """Make a text font fall back to the emoji font of the same size."""
    src = path.read_text()
    emoji = f"avo_emoji_{px}"
    src = src.replace("/*Initialize a public general font descriptor*/",
                      f"LV_FONT_DECLARE({emoji});\n/*Initialize a public general font descriptor*/", 1)
    src = src.replace("    .dsc = &font_dsc           /*The custom font data.",
                      f"    .fallback = &{emoji},\n    .dsc = &font_dsc           /*The custom font data.", 1)
    path.write_text(src)


def run_emoji_conv() -> None:
    for px in EMOJI_SIZES:
        name = f"avo_emoji_{px}"
        print("font", name, flush=True)
        subprocess.run(LV_FONT_CONV + [
            "--bpp", "4", "--size", str(px), "--format", "lvgl", "--lv-include", "lvgl.h",
            "--font", rel(FONT_SRC / EMOJI_TTF), "-r", EMOJI_RANGE,
            "-o", rel(OUT_FONTS / f"{name}.c"),
        ], check=True, cwd=ROOT)


def run_font_conv() -> None:
    OUT_FONTS.mkdir(parents=True, exist_ok=True)
    run_emoji_conv()
    fa = FONT_SRC / "FontAwesome5-Solid+Brands+Regular.woff"
    for name, ttf, px, rng, symbols in FONTS:
        cmd = LV_FONT_CONV + [
            "--bpp", "4", "--size", str(px), "--no-compress", "--format", "lvgl",
            "--lv-include", "lvgl.h",
            "--font", rel(FONT_SRC / ttf), "-r", rng,
        ]
        if symbols:
            cmd += ["--font", rel(fa), "-r", SYMBOLS]
        cmd += ["-o", rel(OUT_FONTS / f"{name}.c")]
        print("font", name, flush=True)
        subprocess.run(cmd, check=True, cwd=ROOT)
        if px in EMOJI_SIZES:
            link_fallback(OUT_FONTS / f"{name}.c", px)


# ---------------------------------------------------------------- shapes
def egg_polygon(cx: float, cy: float, a: float, b: float, steps: int = 240) -> list[tuple[float, float]]:
    """Avocado outline: an egg, narrower at the top. a=half width, b=half height."""
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        y = -math.cos(t)  # -1 top .. 1 bottom
        x = math.sin(t)
        width = 0.80 + 0.20 * y  # narrow top, wide bottom
        pts.append((cx + a * x * width, cy + b * y))
    return pts


def draw_avocado(size_h: int, ears: bool) -> Image.Image:
    """Front view of a halved avocado. Optional cat ears for the avocatOS mark."""
    h = size_h * SS
    w = int(h * 0.82)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx = w / 2
    ear_room = h * 0.07 if ears else 0
    b = (h - ear_room) / 2 * 0.98
    cy = ear_room + b + h * 0.005
    a = w / 2 * 0.98
    if ears:
        # small rounded ears sitting on the shoulders of the egg
        for side in (-1, 1):
            base_in = (cx + side * a * 0.04, cy - b * 0.90)
            base_out = (cx + side * a * 0.46, cy - b * 0.62)
            tip = (cx + side * a * 0.36, cy - b - ear_room * 0.85)
            d.polygon([base_in, tip, base_out], fill=SKIN)
    d.polygon(egg_polygon(cx, cy, a, b), fill=SKIN)
    d.polygon(egg_polygon(cx, cy + b * 0.02, a * 0.90, b * 0.91), fill=RIND)
    d.polygon(egg_polygon(cx, cy + b * 0.03, a * 0.82, b * 0.84), fill=FLESH)
    pr = a * 0.42
    py = cy + b * 0.30
    d.ellipse([cx - pr, py - pr * 1.08, cx + pr, py + pr * 1.08], fill=PIT)
    hr = pr * 0.28
    d.ellipse([cx - pr * 0.45 - hr, py - pr * 0.50 - hr, cx - pr * 0.45 + hr, py - pr * 0.50 + hr], fill=PIT_HI)
    return img.resize((w // SS, h // SS), Image.LANCZOS)


def digit_mask(ch: str, px_h: int, font_path: Path) -> Image.Image:
    font = ImageFont.truetype(str(font_path), int(px_h * 1.38 * SS))
    canvas = Image.new("L", (int(px_h * 1.2 * SS), int(px_h * 1.6 * SS)), 0)
    ImageDraw.Draw(canvas).text((0, 0), ch, font=font, fill=255)
    bbox = canvas.getbbox()
    glyph = canvas.crop(bbox)
    scale = px_h / (glyph.height / SS) / SS
    return glyph.resize((max(1, round(glyph.width * scale)), px_h), Image.LANCZOS)


# ---------------------------------------------------------------- C writers
def c_array(name: str, data: bytes) -> str:
    rows = []
    for i in range(0, len(data), 24):
        rows.append("    " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    return f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}_map[] = {{\n" + "\n".join(rows) + "\n};\n"


def descriptor(name: str, cf: str, w: int, h: int, stride: int) -> str:
    return (
        f"const lv_image_dsc_t {name} = {{\n"
        f"    .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = {cf}, .flags = 0,\n"
        f"                .w = {w}, .h = {h}, .stride = {stride} }},\n"
        f"    .data_size = sizeof({name}_map),\n"
        f"    .data = {name}_map,\n}};\n"
    )


def rgb565a8(img: Image.Image) -> bytes:
    px = img.load()
    w, h = img.size
    color = bytearray()
    alpha = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            color += bytes((v & 0xFF, v >> 8))  # little endian, as lv_color16_t
            alpha.append(a)
    return bytes(color + alpha)


def write_image(name: str, img: Image.Image, fmt: str) -> None:
    OUT_IMG.mkdir(parents=True, exist_ok=True)
    w, h = img.size
    if fmt == "RGB565A8":
        data, cf, stride = rgb565a8(img.convert("RGBA")), "LV_COLOR_FORMAT_RGB565A8", w * 2
    else:
        data, cf, stride = img.convert("L").tobytes(), "LV_COLOR_FORMAT_A8", w
    src = (
        "/* Generated by tools/gen_assets.py — do not edit. */\n"
        '#include "lvgl.h"\n\n' + c_array(name, data) + "\n" + descriptor(name, cf, w, h, stride)
    )
    (OUT_IMG / f"{name}.c").write_text(src)
    print("img ", name, f"{w}x{h}", fmt, flush=True)


def main() -> int:
    if "--no-fonts" not in sys.argv:
        run_font_conv()
    write_image("avo_img_mark_160", draw_avocado(160, ears=True), "RGB565A8")
    write_image("avo_img_mark_56", draw_avocado(56, ears=True), "RGB565A8")
    write_image("avo_img_half_300", draw_avocado(300, ears=False), "RGB565A8")
    write_image("avo_img_half_36", draw_avocado(36, ears=False), "RGB565A8")
    nunito = FONT_SRC / "nunito-900.ttf"
    for digit in "0123456789":
        write_image(f"avo_img_flux_{digit}", digit_mask(digit, 196, nunito), "A8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
