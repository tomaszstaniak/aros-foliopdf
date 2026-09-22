#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write pages12.pdf: a twelve-page fixture made for this project (no
third-party content). Pages differ in size, orientation and colour so that a
wrong page or a wrong fit is obvious in a screenshot. Sizes cycle through
400x300, 300x400, 400x400; the bar hue advances by 30 degrees per page.
Long enough to make the thumbnail sidebar scroll.

"""
import sys
from pathlib import Path

import colorsys
SIZES = [(400, 300), (300, 400), (400, 400)]
pages = []
for k in range(12):
    r, g, b = colorsys.hsv_to_rgb(k / 12.0, 0.9, 0.85)
    pages.append((SIZES[k % 3], f"{r:.2f} {g:.2f} {b:.2f}"))
n = len(pages)
objs = {1: b"<< /Type /Catalog /Pages 2 0 R >>"}
font = 3
kids = []
num = 4
for i, ((w, h), rgb) in enumerate(pages, 1):
    content = (f"BT /F1 32 Tf 30 {h - 70} Td (page {i} of {n}) Tj ET\n"
               f"{rgb} rg 30 30 {w - 60} 40 re f\n"
               f"0 0 0 RG 2 w 1 1 {w - 2} {h - 2} re S\n").encode()
    page_no, cont_no = num, num + 1
    num += 2
    kids.append(page_no)
    objs[page_no] = (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {w} {h}] "
                     f"/Contents {cont_no} 0 R /Resources << /Font << /F1 {font} 0 R >> >> >>").encode()
    objs[cont_no] = b"<< /Length %d >>\nstream\n" % len(content) + content + b"endstream"
objs[2] = ("<< /Type /Pages /Kids [" + " ".join(f"{k} 0 R" for k in kids) + f"] /Count {n} >>").encode()
objs[font] = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>"

out = bytearray(b"%PDF-1.4\n")
offsets = {}
for k in sorted(objs):
    offsets[k] = len(out)
    out += b"%d 0 obj\n" % k + objs[k] + b"\nendobj\n"
xref = len(out)
size = max(objs) + 1
out += b"xref\n0 %d\n" % size + b"0000000000 65535 f \n"
for k in range(1, size):
    out += b"%010d 00000 n \n" % offsets[k]
out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (size, xref)
dest = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("pages12.pdf")
dest.write_bytes(out)
print(dest, len(out), "bytes")
