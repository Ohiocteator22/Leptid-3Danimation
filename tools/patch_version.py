#!/usr/bin/env python3
"""Leptid — stamp a release version into the files where it is hardcoded.

Usage: python3 tools/patch_version.py MAJOR.MINOR.PATCH

Rewrites:
  src/core/version.cpp   kVersion string (shown in the title bar / --help)
  packaging/leptid.rc    FILEVERSION / PRODUCTVERSION + version strings

The release workflow runs this with the pushed git tag (v-prefixed stripped),
so binaries and the Windows exe metadata always match the release tag.
"""
import pathlib
import re
import sys


def main() -> int:
    if len(sys.argv) != 2 or not re.match(r"^\d+\.\d+\.\d+$", sys.argv[1]):
        print("usage: patch_version.py MAJOR.MINOR.PATCH  (e.g. 0.1.2)")
        return 2
    ver = sys.argv[1]
    a, b, c = ver.split(".")
    root = pathlib.Path(__file__).resolve().parent.parent

    vc = root / "src" / "core" / "version.cpp"
    if vc.exists():
        s = vc.read_text()
        s, n = re.subn(r'(kVersion\s*=\s*")[^"]*(")', rf"\g<1>{ver}\g<2>", s)
        vc.write_text(s)
        print(f"version.cpp: {n} replacement(s)")
        if n == 0:
            print(f"  WARNING: kVersion pattern not found in {vc}")

    rc = root / "packaging" / "leptid.rc"
    if rc.exists():
        t = rc.read_text()
        t, n1 = re.subn(r"(FILEVERSION\s+)\d+,\s*\d+,\s*\d+,\s*\d+",
                        rf"\g<1>{a}, {b}, {c}, 0", t)
        t, n2 = re.subn(r"(PRODUCTVERSION\s+)\d+,\s*\d+,\s*\d+,\s*\d+",
                        rf"\g<1>{a}, {b}, {c}, 0", t)
        t, n3 = re.subn(r'(VALUE "FileVersion",\s*)"[^"]*"', rf'\g<1>"{ver}"', t)
        t, n4 = re.subn(r'(VALUE "ProductVersion",\s*)"[^"]*"', rf'\g<1>"{ver}"', t)
        rc.write_text(t)
        print(f"leptid.rc: {n1 + n2 + n3 + n4} replacement(s)")
        if n1 + n2 + n3 + n4 != 4:
            print("  WARNING: expected 4 fields in the .rc, got", n1 + n2 + n3 + n4)

    print(f"patched to {ver}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
