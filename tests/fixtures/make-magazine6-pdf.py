#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write magazine6.pdf: a six-page fixture made for this project (no
third-party content) for the Magazine view. In Magazine it lays out as the
cover alone on the right, then 2-3, 4-5 and 6 alone on the left.

  pages 1, 2, 4, 5, 6: 300x400 portrait; page 3: 400x300 landscape, so the
  spread 2-3 has pages of different proportions.
  Every page: "page N of 6", two lines of text to select, a coloured bar.
  Page 1 has an internal link, "go to page 5", to page 5 (the right page of
  the spread 4-5). The outline has "Chapter 3" -> page 3 and "Chapter 5"
  -> page 5, both right pages.
"""
import sys
from pathlib import Path

SIZES = [(300, 400), (300, 400), (400, 300), (300, 400), (300, 400), (300, 400)]
COLOURS = ["1 0 0", "0.9 0.5 0", "0 0.6 0", "0 0.5 0.8", "0 0 1", "0.6 0 0.6"]
n = len(SIZES)
objs = {1: b"<< /Type /Catalog /Pages 2 0 R /Outlines 3 0 R /PageMode /UseOutlines >>"}
FONT = 4
first_page_obj = 10
page_obj = [first_page_obj + 3 * i for i in range(n)]    # page, contents, annots
for i, ((w, h), rgb) in enumerate(zip(SIZES, COLOURS)):
    lines = [f"BT /F1 28 Tf 20 {h - 60} Td (page {i + 1} of {n}) Tj ET",
             f"BT /F2 14 Tf 20 {h - 100} Td (Selectable text on page {i + 1},) Tj ET",
             f"BT /F2 14 Tf 20 {h - 120} Td (second line of page {i + 1}.) Tj ET",
             f"{rgb} rg 20 20 {w - 40} 30 re f",
             f"0 0 0 RG 2 w 1 1 {w - 2} {h - 2} re S"]
    if i == 0:
        lines.append(f"BT /F2 14 Tf 20 {h - 170} Td (go to page 5) Tj ET")
    content = ("\n".join(lines) + "\n").encode()
    p, c, a = page_obj[i], page_obj[i] + 1, page_obj[i] + 2
    annots = f"/Annots {a} 0 R " if i == 0 else ""
    objs[p] = (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {w} {h}] {annots}"
               f"/Contents {c} 0 R /Resources << /Font << /F1 {FONT} 0 R /F2 {FONT + 1} 0 R >> >> >>").encode()
    objs[c] = b"<< /Length %d >>\nstream\n" % len(content) + content + b"endstream"
    if i == 0:
        objs[a] = (f"[ << /Type /Annot /Subtype /Link /Rect [18 {h - 176} 120 {h - 154}] /Border [0 0 0] "
                   f"/Dest [{page_obj[4]} 0 R /XYZ 0 {SIZES[4][1]} 0] >> ]").encode()
objs[2] = ("<< /Type /Pages /Kids [" + " ".join(f"{k} 0 R" for k in page_obj) + f"] /Count {n} >>").encode()
objs[FONT] = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>"
objs[FONT + 1] = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
# Outline: 3 is the root, 6 and 7 its two entries.
objs[3] = b"<< /Type /Outlines /First 6 0 R /Last 7 0 R /Count 2 >>"
objs[6] = (f"<< /Title (Chapter 3) /Parent 3 0 R /Next 7 0 R "
           f"/Dest [{page_obj[2]} 0 R /XYZ 0 {SIZES[2][1]} 0] >>").encode()
objs[7] = (f"<< /Title (Chapter 5) /Parent 3 0 R /Prev 6 0 R "
           f"/Dest [{page_obj[4]} 0 R /XYZ 0 {SIZES[4][1]} 0] >>").encode()

out = bytearray(b"%PDF-1.4\n")
offsets = {}
for k in sorted(objs):
    offsets[k] = len(out)
    out += b"%d 0 obj\n" % k + objs[k] + b"\nendobj\n"
xref = len(out)
size = max(objs) + 1
out += b"xref\n0 %d\n" % size
for k in range(0, size):
    if k in offsets:
        out += b"%010d 00000 n \n" % offsets[k]
    else:
        out += b"0000000000 65535 f \n"
out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (size, xref)
dest = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("magazine6.pdf")
dest.write_bytes(out)
print(dest, len(out), "bytes")
