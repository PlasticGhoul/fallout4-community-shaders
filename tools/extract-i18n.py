#!/usr/bin/env python3
"""Generate the English translation catalogue from the source tree.

Every user-visible string is written in one of three forms, and all three have
the same shape - a key, then the English text:

    T("menu.close", "Close")
    .Label("setting.menu.font_size", "Font size")
    .Help("setting.menu.font_size.help", "Size of the text in this overlay.")

The keys are written out rather than derived from a setting's path on purpose.
A derivation would have to be kept identical here and in C++, and a drift
between the two loses a translation without breaking anything.

    python tools/extract-i18n.py            # check, and report any difference
    python tools/extract-i18n.py --write    # regenerate en.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOT = REPO_ROOT / "src"
CATALOGUE = (
    REPO_ROOT
    / "package"
    / "F4SE"
    / "Plugins"
    / "CommunityShadersFO4"
    / "Translations"
    / "en.json"
)

# One pattern for all three forms, which is the whole reason the keys are
# spelled out. \s* throughout because clang-format wraps a long call across
# lines, and \s already matches a newline.
PATTERN = re.compile(
    r'(?:\bT|\.Label|\.Help)\s*\(\s*'
    r'"((?:[^"\\]|\\.)*)"\s*,\s*'
    r'"((?:[^"\\]|\\.)*)"\s*\)'
)

META = {
    "language": "English",
    "locale": "en",
    "auto_generated": True,
    "generator": "tools/extract-i18n.py",
    "note": "DO NOT EDIT MANUALLY. Run: python tools/extract-i18n.py --write",
}


def unescape(text: str) -> str:
    """Turn a C++ string literal's body into the text it denotes."""
    return text.encode("ascii", "backslashreplace").decode("unicode_escape")


def collect() -> dict[str, str]:
    found: dict[str, str] = {}
    conflicts: list[str] = []

    for path in sorted(SOURCE_ROOT.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue

        text = path.read_text(encoding="utf-8")
        for key, english in PATTERN.findall(text):
            key = unescape(key)
            english = unescape(english)

            previous = found.get(key)
            if previous is not None and previous != english:
                # Never resolved by picking one: whichever the scan happened to
                # reach last would win, and the translator would be handed a
                # key whose meaning depends on file order.
                conflicts.append(
                    f"{key}: {previous!r} in one place, {english!r} in {path.relative_to(REPO_ROOT)}"
                )
                continue

            found[key] = english

    if conflicts:
        print("the same key is used for different English text:", file=sys.stderr)
        for conflict in conflicts:
            print(f"  {conflict}", file=sys.stderr)
        sys.exit(1)

    return found


def render(strings: dict[str, str]) -> str:
    document = {"_meta": META}
    document.update({key: strings[key] for key in sorted(strings)})
    return json.dumps(document, indent=4, ensure_ascii=False) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--write",
        action="store_true",
        help="regenerate en.json instead of only checking it",
    )
    args = parser.parse_args()

    strings = collect()
    rendered = render(strings)

    if args.write:
        CATALOGUE.parent.mkdir(parents=True, exist_ok=True)
        # newline="" so Python does not translate \n to \r\n on Windows: the
        # repo is LF, and a generated file that arrives as CRLF would be
        # rewritten by the pre-commit hook on every run.
        with CATALOGUE.open("w", encoding="utf-8", newline="") as stream:
            stream.write(rendered)
        print(f"wrote {len(strings)} key(s) to {CATALOGUE.relative_to(REPO_ROOT)}")
        return 0

    current = CATALOGUE.read_text(encoding="utf-8") if CATALOGUE.exists() else ""
    if current == rendered:
        print(f"{len(strings)} key(s), en.json is up to date")
        return 0

    existing = json.loads(current) if current else {}
    existing.pop("_meta", None)

    for key in sorted(set(strings) - set(existing)):
        print(f"+ {key}: {strings[key]!r}")
    for key in sorted(set(existing) - set(strings)):
        print(f"- {key}")
    for key in sorted(set(strings) & set(existing)):
        if strings[key] != existing[key]:
            print(f"~ {key}: {existing[key]!r} -> {strings[key]!r}")

    print("\nen.json is out of date. Run: python tools/extract-i18n.py --write")
    return 1


if __name__ == "__main__":
    sys.exit(main())
