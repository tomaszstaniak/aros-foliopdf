#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write hello.pdf: a one-page fixture made for this project (no third-party
content). It uses a base-14 font, so rendering exercises MuPDF's bundled URW
fonts and FreeType, plus filled and stroked vector paths.

Expected: mutool info reports 1 page, MediaBox [0 0 400 300], font Helvetica.
Rendered: text "Folio fixture 001" in black near the top, a red filled
rectangle bottom-left, a blue stroked triangle bottom-right.
"""
import sys
from pathlib import Path

content = b"""BT /F1 28 Tf 30 240 Td (Folio fixture 001) Tj ET
1 0 0 rg 30 40 140 120 re f
0 0 1 RG 6 w 230 40 m 370 40 l 300 160 l h S
"""
objs = [
    b"<< /Type /Catalog /Pages 2 0 R >>",
    b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] /Contents 4 0 R "
    b"/Resources << /Font << /F1 5 0 R >> >> >>",
    b"<< /Length %d >>\nstream\n" % len(content) + content + b"endstream",
    b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
]
out = bytearray(b"%PDF-1.4\n")
offsets = []
for i, body in enumerate(objs, 1):
    offsets.append(len(out))
    out += b"%d 0 obj\n" % i + body + b"\nendobj\n"
xref = len(out)
out += b"xref\n0 %d\n" % (len(objs) + 1) + b"0000000000 65535 f \n"
for off in offsets:
    out += b"%010d 00000 n \n" % off
out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, xref)
dest = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("hello.pdf")
dest.write_bytes(out)
print(dest, len(out), "bytes")
