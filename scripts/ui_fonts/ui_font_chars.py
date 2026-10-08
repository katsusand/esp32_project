"""Which characters the UI text fonts must contain.

Shared by gen_ui_fonts.py (which needs freetype) and check_ui_font_glyphs.py
(which runs inside every firmware build and therefore uses the standard library
only).

Two sets make up the text fonts:

- required: ASCII, every non-ASCII character inside a C string or character
  literal under components/ and main/, and extra_chars.txt for text that is
  only ever assembled at run time. A required character the TTF lacks is an
  error.
- optional: kana, Japanese punctuation and full-width forms, so ordinary text
  added later usually renders before anyone regenerates. Characters the TTF
  lacks are simply left out.

The 16px regular face can additionally carry all of JIS X 0208
(jis_x0208_chars, enabled by font_profile.json), for text that comes from
outside the firmware.

English contract: only literals count. Comments are stripped first, so a
Japanese comment never pulls glyphs into the firmware, and a Japanese string can
never be missing from the font just because it sits next to a comment.
"""

from __future__ import annotations

import pathlib
import re
import unicodedata

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_DIRS = ("components", "main")
SOURCE_SUFFIXES = (".c", ".h", ".cpp", ".hpp")
EXTRA_CHARS_FILE = pathlib.Path(__file__).with_name("extra_chars.txt")

# Generated tables quote no UI text, and scanning their byte arrays is slow.
EXCLUDED_DIRS = ("components/support/cyd_ui_fonts/generated",)


def _ranges(*pairs: tuple[int, int]) -> set[str]:
    out: set[str] = set()
    for first, last in pairs:
        out.update(chr(c) for c in range(first, last + 1))
    return out


ASCII_CHARS: set[str] = _ranges((0x0020, 0x007E))  # URL, SSID, codes, IP addresses

OPTIONAL_CHARS: set[str] = _ranges(
    (0x3000, 0x303F),  # CJK symbols and punctuation
    (0x3041, 0x3096),  # hiragana
    (0x309B, 0x309E),
    (0x30A1, 0x30FE),  # katakana, including ー and ・
    (0xFF01, 0xFF5E),  # full-width ASCII forms
) | set("…‥―‐–—°×÷±←↑→↓○●◎△▲▽▼□■◇◆★☆※")

def jis_x0208_chars() -> set[str]:
    """Every character of JIS X 0208: kana, symbols, and level 1 and 2 kanji.

    Only the 16px regular face carries these, and only when font_profile.json
    sets body_jis_x0208: it is the face used for text the firmware does not
    know in advance, such as a message from a server. Both common Unicode mappings are
    included, since text typed on Windows uses U+FF5E for the wave dash and
    text typed elsewhere uses U+301C.
    """
    chars: set[str] = set()
    for row in range(1, 95):
        for cell in range(1, 95):
            try:
                ch = bytes([0xA0 + row, 0xA0 + cell]).decode("euc_jp")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1:
                chars.add(ch)
    return chars | set("～－∥￠￡￢")


# The clock fonts carry digits and separators only; that is what keeps a 64px
# face down to a few kilobytes. The two result marks ride along because they
# are wanted at the same large size.
CLOCK_CHARS: set[str] = set("0123456789:/-. ✓!")

_COMMENT_OR_LITERAL = re.compile(
    r"""
      //[^\n]*                     # line comment
    | /\*.*?\*/                    # block comment
    | "(?:\\.|[^"\\\n])*"          # string literal
    | '(?:\\.|[^'\\\n])*'          # character literal
    """,
    re.DOTALL | re.VERBOSE,
)


def _literal_chars(text: str) -> set[str]:
    chars: set[str] = set()
    for match in _COMMENT_OR_LITERAL.finditer(text):
        token = match.group(0)
        if token[0] in "\"'":
            chars.update(ch for ch in token[1:-1] if ord(ch) > 0x7E)
    return chars


def source_files() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for top in SOURCE_DIRS:
        for path in sorted((REPO_ROOT / top).rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            rel = path.relative_to(REPO_ROOT).as_posix()
            if any(rel.startswith(ex + "/") for ex in EXCLUDED_DIRS):
                continue
            files.append(path)
    return files


def literal_chars_by_file() -> dict[pathlib.Path, set[str]]:
    found: dict[pathlib.Path, set[str]] = {}
    for path in source_files():
        chars = _literal_chars(path.read_text(encoding="utf-8", errors="replace"))
        chars.discard("�")
        if chars:
            found[path] = chars
    return found


def extra_chars() -> set[str]:
    if not EXTRA_CHARS_FILE.exists():
        return set()
    chars: set[str] = set()
    for line in EXTRA_CHARS_FILE.read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            continue
        chars.update(ch for ch in line if not ch.isspace())
    return chars


def _renderable(chars: set[str]) -> set[str]:
    # Control and combining characters never render as glyphs of their own.
    # Spaces do: U+3000 is the full-width space Japanese layout relies on.
    return {ch for ch in chars if ch.isprintable() or unicodedata.category(ch) == "Zs"}


def required_text_chars() -> set[str]:
    chars = set(ASCII_CHARS) | extra_chars()
    for found in literal_chars_by_file().values():
        chars |= found
    return _renderable(chars)


def optional_text_chars() -> set[str]:
    return _renderable(OPTIONAL_CHARS)
