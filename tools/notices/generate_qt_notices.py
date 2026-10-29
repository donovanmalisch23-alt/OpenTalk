#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes the notices for the third-party code inside Qt that OpenTalk's Windows
package ships, from Qt's own attribution files.

Usage: generate_qt_notices.py <qtbase checkout at the shipped tag> <output folder>

The qtbase checkout must be at the Qt version the package uses (for example
`git clone --depth 1 --branch v6.8.3 https://github.com/qt/qtbase`). The output is
`Qt-third-party.txt` plus one file per licence in `<output>/texts/`. Components that
cannot be in a Windows build (Android, macOS, X11, WebAssembly, D-Bus, build-system and
test-only code) are left out; anything uncertain is kept, since over-attribution is
harmless.
"""
import json
import pathlib
import re
import sys

# Directories (relative to qtbase) whose code is not in OpenTalk's Windows binaries.
EXCLUDED = (
    "cmake/",  # build system only
    "src/3rdparty/android", "src/3rdparty/gradle",  # Android
    "src/3rdparty/forkfd",  # Unix only
    "src/3rdparty/wasm",  # WebAssembly fonts
    "src/3rdparty/xcb",  # X11
    "src/3rdparty/pixman",  # ARM NEON only
    "src/corelib/kernel",  # macOS event dispatcher
    "src/dbus",  # Qt D-Bus is not shipped
    "src/plugins/platforms/cocoa",  # macOS
    "src/testinternal/", "src/testlib/",  # Qt Test is not shipped
)


def licence_ids(expression):
    """The individual licence identifiers in an SPDX expression."""
    return sorted({t for t in re.split(r"[\s()]+", expression or "") if t and t not in ("AND", "OR", "WITH")})


def main():
    qtbase = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2])
    (out / "texts").mkdir(parents=True, exist_ok=True)
    entries = []
    for path in sorted(qtbase.rglob("qt_attribution.json")):
        rel = path.parent.relative_to(qtbase).as_posix() + "/"
        if rel.startswith(EXCLUDED):
            continue
        data = json.loads(path.read_text(encoding="utf-8"), strict=False)
        for item in data if isinstance(data, list) else [data]:
            entries.append((rel, item))
    used = set()
    lines = [
        "Third-party code inside Qt " + sys.argv[3] if len(sys.argv) > 3 else "Third-party code inside Qt",
        "=" * 60,
        "",
        "Qt (qt.io) contains the following third-party components, which are part of the",
        "Qt libraries and plugins shipped with OpenTalk. Their licence texts are in the",
        "\"texts\" folder next to this file, named by SPDX identifier.",
        "",
    ]
    for rel, item in sorted(entries, key=lambda e: (e[1].get("Name") or "").lower()):
        ids = licence_ids(item.get("LicenseId"))
        used.update(ids)
        lines.append("-" * 60)
        lines.append(item.get("Name") or item.get("Id"))
        if item.get("Version"):
            lines.append("Version: " + str(item["Version"]))
        if item.get("Homepage"):
            lines.append("Homepage: " + item["Homepage"])
        if item.get("QtUsage"):
            lines.append("Used in Qt: " + " ".join(item["QtUsage"].split()))
        lines.append("Licence: " + (item.get("LicenseId") or item.get("License") or "see Qt"))
        copyright = item.get("Copyright")
        if isinstance(copyright, list):
            copyright = "\n".join(copyright)
        if copyright:
            lines.append("Copyright:")
            lines.extend("  " + c.strip() for c in copyright.splitlines() if c.strip())
        lines.append("")
    (out / "Qt-third-party.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    used.update({"LGPL-3.0-only", "GPL-3.0-only"})  # Qt itself
    for licence in sorted(used):
        source = qtbase / "LICENSES" / (licence + ".txt")
        if source.exists():
            (out / "texts" / (licence + ".txt")).write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
        else:
            print("warning: no text for", licence, file=sys.stderr)
    print(len(entries), "components,", len(used), "licences")


if __name__ == "__main__":
    main()
