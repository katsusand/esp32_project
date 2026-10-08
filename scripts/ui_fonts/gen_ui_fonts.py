#!/usr/bin/env python3
"""Generates the anti-aliased UI font tables in components/support/cyd_ui_fonts.

Usage (from the repository root):

    scripts/ui_fonts/fetch_fonts.sh
    python3 -m venv scripts/.venv
    scripts/.venv/bin/pip install -r scripts/ui_fonts/requirements.txt
    scripts/.venv/bin/python scripts/ui_fonts/gen_ui_fonts.py

The output is committed, so a firmware build never needs freetype. Rerun this
whenever the firmware build reports a character missing from the UI fonts.
See cyd_ui_fonts.h for the table format.

font_profile.json next to this script picks what a project needs beyond its
own strings (see PROFILE_DEFAULTS).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

import freetype

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ui_font_chars  # noqa: E402

REPO_ROOT = ui_font_chars.REPO_ROOT
FONT_DIR = REPO_ROOT / "third_party/fonts/biz_udpgothic"
OUT_DIR = REPO_ROOT / "components/support/cyd_ui_fonts/generated"
MANIFEST = OUT_DIR / "text_font_chars.txt"
# For a server-side editor: what the message face can draw, and how wide.
MESSAGE_FONT_JSON = OUT_DIR / "message_font.json"
PROFILE_FILE = pathlib.Path(__file__).with_name("font_profile.json")

# body_jis_x0208: the 16px regular face also carries all of JIS X 0208, for
# text that reaches the screen from outside the firmware (a server message).
# It costs about 800KB of flash, and also writes MESSAGE_FONT_JSON.
PROFILE_DEFAULTS = {
    "body_jis_x0208": False,
}

LICENSE_NOTE = (
    "Subset of BIZ UDPGothic, Copyright 2022 The BIZ UDGothic Project Authors,\n"
    " * licensed under the SIL Open Font License 1.1.\n"
    " * See third_party/fonts/biz_udpgothic/OFL.txt."
)

# (C symbol suffix, TTF, pixel size, character set)
#
# "text" is what the firmware's own strings need; "body" is "text", plus all of
# JIS X 0208 when the profile asks for it.
FONTS = (
    ("body", "BIZUDPGothic-Regular.ttf", 16, "body"),
    ("body_bold", "BIZUDPGothic-Bold.ttf", 16, "text"),
    ("title", "BIZUDPGothic-Bold.ttf", 24, "text"),
    ("clock_medium", "BIZUDPGothic-Bold.ttf", 48, "clock"),
    ("clock_large", "BIZUDPGothic-Bold.ttf", 64, "clock"),
)


def quantize(value: int, gamma: float) -> int:
    """8-bit coverage to 4 bits. gamma < 1 thickens strokes slightly."""
    if value == 0:
        return 0
    level = round(((value / 255.0) ** gamma) * 15.0)
    return max(1, min(15, level))


def render_font(symbol: str, ttf: str, px: int, chars: list[str], optional: set[str],
                gamma: float):
    face = freetype.Face(str(FONT_DIR / ttf))
    face.set_pixel_sizes(0, px)
    ascent = round(px * face.ascender / face.units_per_EM)
    line_height = round(px * (face.ascender - face.descender) / face.units_per_EM)
    flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT

    codepoints: list[int] = []
    glyphs: list[tuple[int, int, int, int, int, int]] = []
    bitmaps = bytearray()
    missing: list[str] = []

    for ch in chars:
        cp = ord(ch)
        # LovyanGFX decodes text to 16-bit code units, so the BMP is the limit.
        if cp > 0xFFFF or (not ch.isspace() and face.get_char_index(cp) == 0):
            if ch not in optional:
                missing.append(ch)
            continue
        face.load_char(ch, flags)
        g = face.glyph
        bm = g.bitmap
        width, height = bm.width, bm.rows
        advance = (g.advance.x + 32) >> 6
        x_offset = g.bitmap_left
        y_offset = ascent - g.bitmap_top
        if width == 0 or height == 0:
            width = height = x_offset = y_offset = 0

        for name, value, lo, hi in (
            ("width", width, 0, 255),
            ("height", height, 0, 255),
            ("x_advance", advance, 0, 255),
            ("x_offset", x_offset, -128, 127),
            ("y_offset", y_offset, -128, 127),
        ):
            if not lo <= value <= hi:
                raise SystemExit(f"{symbol}: U+{cp:04X} {name}={value} out of range")

        offset = len(bitmaps)
        nibbles: list[int] = []
        for row in range(height):
            base = row * bm.pitch
            nibbles.extend(quantize(bm.buffer[base + col], gamma) for col in range(width))
        if len(nibbles) % 2:
            nibbles.append(0)
        bitmaps.extend((nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2))

        codepoints.append(cp)
        glyphs.append((offset, width, height, advance, x_offset, y_offset))

    return {
        "symbol": symbol,
        "ttf": ttf,
        "px": px,
        "ascent": ascent,
        "line_height": line_height,
        "codepoints": codepoints,
        "glyphs": glyphs,
        "bitmaps": bytes(bitmaps),
        "missing": missing,
    }


def describe(cp: int) -> str:
    ch = chr(cp)
    if ch in "\\*/" or not ch.isprintable():
        return f"U+{cp:04X}"
    return f"U+{cp:04X} {ch}"


def emit_c(font: dict) -> str:
    sym = font["symbol"]
    n = len(font["codepoints"])
    out: list[str] = []
    out.append("/*")
    out.append(" * Generated by scripts/ui_fonts/gen_ui_fonts.py. Do not edit.")
    out.append(f" * {font['ttf']} {font['px']}px, {n} glyphs, "
               f"{len(font['bitmaps'])} bitmap bytes.")
    out.append(" *")
    out.append(" * " + LICENSE_NOTE)
    out.append(" */")
    out.append('#include "cyd_ui_fonts.h"')
    out.append("")
    out.append(f"static const uint16_t s_codepoints[{n}] = {{")
    for i in range(0, n, 12):
        chunk = font["codepoints"][i:i + 12]
        out.append("    " + ", ".join(f"0x{cp:04x}" for cp in chunk) + ",")
    out.append("};")
    out.append("")
    out.append(f"static const cyd_ui_font_glyph_t s_glyphs[{n}] = {{")
    for cp, (off, w, h, adv, dx, dy) in zip(font["codepoints"], font["glyphs"]):
        out.append(f"    {{ {off}u, {w}, {h}, {adv}, {dx}, {dy} }}, /* {describe(cp)} */")
    out.append("};")
    out.append("")
    data = font["bitmaps"]
    out.append(f"static const uint8_t s_bitmaps[{max(1, len(data))}] = {{")
    for i in range(0, len(data), 24):
        out.append("    " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    if not data:
        out.append("    0")
    out.append("};")
    out.append("")
    out.append(f"const cyd_ui_font_t cyd_ui_font_{sym} = {{")
    out.append(f'    .name = "{sym}",')
    out.append(f"    .pixel_size = {font['px']},")
    out.append(f"    .ascent = {font['ascent']},")
    out.append(f"    .line_height = {font['line_height']},")
    out.append(f"    .glyph_count = {n},")
    out.append("    .codepoints = s_codepoints,")
    out.append("    .glyphs = s_glyphs,")
    out.append("    .bitmaps = s_bitmaps,")
    out.append("};")
    return "\n".join(out) + "\n"


def write_manifest(chars: list[str]) -> None:
    lines = [
        "# Generated by scripts/ui_fonts/gen_ui_fonts.py. Do not edit.",
        "# Characters in the UI text fonts (body, body_bold, title).",
        "# check_ui_font_glyphs.py compares string literals against this list.",
        "# Spaces are not listed; U+0020 and U+3000 are always included.",
    ]
    printable = [ch for ch in chars if not ch.isspace()]
    for i in range(0, len(printable), 40):
        lines.append("".join(printable[i:i + 40]))
    MANIFEST.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_message_font_json(font: dict) -> None:
    """Advance of every character in the message face, keyed by the character.

    The admin frontend measures greetings and announcements with this so it can
    refuse text that would not fit on the device, or that it cannot draw,
    before anyone saves it. The device draws a missing character as a box
    `missing_advance` pixels wide.
    """
    advances = {chr(cp): glyph[3] for cp, glyph in zip(font["codepoints"], font["glyphs"])}
    data = {
        "generator": "scripts/ui_fonts/gen_ui_fonts.py",
        "family": "BIZ UDPGothic",
        "style": "Regular",
        "pixel_size": font["px"],
        "line_height": font["line_height"],
        "missing_advance": advances.get(" ", font["px"] // 4),
        "advances": advances,
    }
    MESSAGE_FONT_JSON.write_text(json.dumps(data, ensure_ascii=False, sort_keys=True,
                                            separators=(",", ":")) + "\n", encoding="utf-8")


def load_profile() -> dict:
    profile = dict(PROFILE_DEFAULTS)
    if PROFILE_FILE.exists():
        loaded = json.loads(PROFILE_FILE.read_text(encoding="utf-8"))
        unknown = sorted(set(loaded) - set(PROFILE_DEFAULTS))
        if unknown:
            raise SystemExit(f"{PROFILE_FILE.name}: unknown keys {unknown}")
        profile.update(loaded)
    return profile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--gamma", type=float, default=0.85,
                        help="coverage gamma; below 1 makes strokes heavier")
    args = parser.parse_args()

    for _, ttf, _, _ in FONTS:
        if not (FONT_DIR / ttf).exists():
            print(f"missing {FONT_DIR / ttf}; run scripts/ui_fonts/fetch_fonts.sh",
                  file=sys.stderr)
            return 1

    profile = load_profile()
    required = ui_font_chars.required_text_chars()
    optional = ui_font_chars.optional_text_chars() - required
    text_chars = sorted(required | optional)
    if profile["body_jis_x0208"]:
        body_optional = (optional | ui_font_chars.jis_x0208_chars()) - required
    else:
        body_optional = optional
    body_chars = sorted(required | body_optional)
    clock_chars = sorted(ui_font_chars.CLOCK_CHARS)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    failed = False
    included_text: set[str] | None = None
    for symbol, ttf, px, charset in FONTS:
        if charset == "body":
            chars, allowed_missing = body_chars, body_optional
        elif charset == "text":
            chars, allowed_missing = text_chars, optional
        else:
            chars, allowed_missing = clock_chars, set()
        font = render_font(symbol, ttf, px, chars, allowed_missing, args.gamma)
        if font["missing"]:
            failed = True
            listed = " ".join(f"U+{ord(c):04X}({c})" for c in font["missing"])
            print(f"{symbol}: not in {ttf}: {listed}", file=sys.stderr)
        if charset in ("text", "body"):
            got = {chr(cp) for cp in font["codepoints"]}
            included_text = got if included_text is None else included_text & got
        if charset == "body" and profile["body_jis_x0208"]:
            write_message_font_json(font)
        path = OUT_DIR / f"cyd_ui_font_{symbol}.c"
        path.write_text(emit_c(font), encoding="utf-8")
        print(f"{path.relative_to(REPO_ROOT)}: {len(font['codepoints'])} glyphs, "
              f"{len(font['bitmaps']) + len(font['codepoints']) * 14} bytes")

    if not profile["body_jis_x0208"]:
        MESSAGE_FONT_JSON.unlink(missing_ok=True)
    write_manifest(sorted(included_text or set()))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
