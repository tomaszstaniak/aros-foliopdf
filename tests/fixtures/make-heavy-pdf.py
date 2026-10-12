#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write heavy.pdf: pages that are slow to rasterise, for render timing.

Eight pages cycle through four kinds:
  vectors  - 2 000 stroked Bezier curves in different colours
  image    - one 2400x3200 RGB image, Flate-compressed
  alpha    - 500 overlapping translucent discs
  light    - a few lines of text, the control

The content is generated from a fixed seed, so the file is the same on
every run. Standard library only.
"""
import sys
import zlib

W, H = 595, 842  # A4 in points


class Lcg:
    def __init__(self, seed):
        self.s = seed

    def next(self):
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return self.s

    def uniform(self, lo, hi):
        return lo + (hi - lo) * (self.next() / 0x7FFFFFFF)


def vectors(rng):
    out = []
    for _ in range(2000):
        out.append("%.3f %.3f %.3f RG %.2f w" % (rng.uniform(0, 1), rng.uniform(0, 1),
                                                rng.uniform(0, 1), rng.uniform(0.2, 1.5)))
        p = [rng.uniform(0, W) if i % 2 == 0 else rng.uniform(0, H) for i in range(8)]
        out.append("%.1f %.1f m %.1f %.1f %.1f %.1f %.1f %.1f c S" % tuple(p))
    return "\n".join(out).encode()


def alpha(rng):
    out = ["/GS1 gs"]
    k = 0.5523
    for _ in range(500):
        x, y, r = rng.uniform(0, W), rng.uniform(0, H), rng.uniform(5, 60)
        out.append("%.3f %.3f %.3f rg" % (rng.uniform(0, 1), rng.uniform(0, 1), rng.uniform(0, 1)))
        c = r * k
        out.append("%.1f %.1f m" % (x + r, y))
        out.append("%.1f %.1f %.1f %.1f %.1f %.1f c" % (x + r, y + c, x + c, y + r, x, y + r))
        out.append("%.1f %.1f %.1f %.1f %.1f %.1f c" % (x - c, y + r, x - r, y + c, x - r, y))
        out.append("%.1f %.1f %.1f %.1f %.1f %.1f c" % (x - r, y - c, x - c, y - r, x, y - r))
        out.append("%.1f %.1f %.1f %.1f %.1f %.1f c f" % (x + c, y - r, x + r, y - c, x + r, y))
    return "\n".join(out).encode()


def image_data(rng):
    iw, ih = 2400, 3200
    rows = []
    for y in range(ih):
        # Bands that change every row and a little noise: big, and not
        # trivially compressible, like a photograph.
        n = rng.next() & 0x1F
        row = bytearray(1 + iw * 3)
        for x in range(iw):
            o = 1 + x * 3
            row[o] = (x * 255 // iw + n) & 0xFF
            row[o + 1] = (y * 255 // ih) & 0xFF
            row[o + 2] = (((x // 16 + y // 16) & 1) * 128 + n) & 0xFF
        rows.append(bytes(row))
    raw = b"".join(rows)
    return iw, ih, zlib.compress(raw, 6)


def light():
    return (b"BT /F1 24 Tf 72 760 Td (Folio render timing: light page) Tj ET\n"
            b"BT /F1 12 Tf 72 730 Td (This page is the control for the heavy ones.) Tj ET\n"
            b"0 0 1 RG 2 w 72 700 m 523 700 l S")


def main(path):
    rng = Lcg(20261012)
    kinds = ["vectors", "image", "alpha", "light"] * 2
    objs = {}   # number -> bytes

    def add(n, body):
        objs[n] = body

    iw, ih, img = image_data(rng)
    add(3, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    add(4, b"<< /Type /ExtGState /ca 0.35 /BM /Normal >>")
    add(5, b"<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB "
           b"/BitsPerComponent 8 /Filter /FlateDecode /DecodeParms << /Predictor 10 /Colors 3 "
           b"/Columns %d >> /Length %d >>\nstream\n" % (iw, ih, iw, len(img)) + img + b"\nendstream")
    res = b"<< /Font << /F1 3 0 R >> /ExtGState << /GS1 4 0 R >> /XObject << /Im1 5 0 R >> >>"
    kids = []
    n = 6
    for kind in kinds:
        if kind == "vectors":
            content = vectors(rng)
        elif kind == "alpha":
            content = alpha(rng)
        elif kind == "image":
            content = b"q %d 0 0 %d 0 0 cm /Im1 Do Q" % (W, H)
        else:
            content = light()
        data = zlib.compress(content, 9)
        add(n, b"<< /Length %d /Filter /FlateDecode >>\nstream\n" % len(data) + data + b"\nendstream")
        add(n + 1, b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Resources %s /Contents %d 0 R >>"
            % (W, H, res, n))
        kids.append(n + 1)
        n += 2
    add(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    add(2, b"<< /Type /Pages /Kids [%s] /Count %d >>" % (b" ".join(b"%d 0 R" % k for k in kids), len(kids)))

    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = {}
    for num in sorted(objs):
        offsets[num] = len(out)
        out += b"%d 0 obj\n" % num + objs[num] + b"\nendobj\n"
    xref = len(out)
    top = max(objs) + 1
    out += b"xref\n0 %d\n0000000000 65535 f \n" % top
    for num in range(1, top):
        out += b"%010d 00000 n \n" % offsets[num]
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (top, xref)
    with open(path, "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "heavy.pdf")
