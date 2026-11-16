#!/usr/bin/env python3
"""Name source asset references in a HOST-only course draw capture."""
from pathlib import Path
import re
import sys


def header(text):
    names = sorted(set(re.findall(r"extern (?:unsigned char|u16) (\w+)\[\];", text)))
    if not names:
        raise ValueError("No source asset declarations")
    return ("/* Generated asset address names; no asset bytes. */\n"
            "static const char* road_asset_name(unsigned int address) {\n" +
            "".join('    if (address == (unsigned int)%s) return "%s";\n' % (n, n)
                    for n in names) + "    return 0;\n}\n")


if __name__ == "__main__":
    text = Path(sys.argv[1]).read_text()
    for path in sys.argv[3:]:
        text += "\n" + Path(path).read_text()
    Path(sys.argv[2]).write_text(header(text), newline="\n")
