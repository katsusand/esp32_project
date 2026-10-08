#!/usr/bin/env python3
"""Fails when a string literal uses a character the UI text fonts lack.

Runs as part of every firmware build (components/support/cyd_ui_fonts), so it
uses the standard library only. A missing glyph would otherwise reach the panel
as an empty box, on a screen nobody happened to open during testing.

Usage: check_ui_font_glyphs.py [--stamp PATH]
"""

from __future__ import annotations

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ui_font_chars  # noqa: E402

MANIFEST = (ui_font_chars.REPO_ROOT
            / "components/support/cyd_ui_fonts/generated/text_font_chars.txt")


def load_manifest() -> set[str]:
    chars = {" ", "\u3000"}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line.startswith("#"):
            chars.update(line)
    return chars


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stamp", type=pathlib.Path,
                        help="file to touch on success (for build systems)")
    args = parser.parse_args()

    available = load_manifest()
    problems: list[str] = []
    for path, chars in sorted(ui_font_chars.literal_chars_by_file().items()):
        lacking = sorted(ch for ch in chars if ch not in available and ch.isprintable())
        if lacking:
            rel = path.relative_to(ui_font_chars.REPO_ROOT)
            problems.append(f"  {rel}: {''.join(lacking)}")
    lacking_extra = sorted(ui_font_chars.extra_chars() - available)
    if lacking_extra:
        problems.append(f"  scripts/ui_fonts/extra_chars.txt: {''.join(lacking_extra)}")

    if problems:
        print("UI fonts lack characters used by the firmware:", file=sys.stderr)
        print("\n".join(problems), file=sys.stderr)
        print("Regenerate them: scripts/.venv/bin/python scripts/ui_fonts/gen_ui_fonts.py "
              "(see docs/cyd_ui_fonts.md)", file=sys.stderr)
        return 1

    if args.stamp is not None:
        args.stamp.parent.mkdir(parents=True, exist_ok=True)
        args.stamp.write_text("ok\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
