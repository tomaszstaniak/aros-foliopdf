#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Turn a PNG into an AROS PNG icon (.info).

    scripts/mkpngicon.py in.png out.info [--size 64] [--stack 8388608]
        [--type tool|project|drawer] [--default-tool PATH] [--tooltype K=V]...

An AROS PNG icon is a PNG file with one extra chunk, `icOn`, holding
big-endian attribute records (icon.library `diskobjPNGio.c`):
  ULONG id, ULONG value          for numeric attributes
  ULONG id, C string             for DEFAULTTOOL and TOOLTYPE
The chunk sits before IEND. icon.library ignores the chunk CRC on read but
one is written anyway, as the library itself does. The image is scaled
with the host's `sips`, alpha kept.
"""
import argparse, struct, subprocess, sys, zlib
from pathlib import Path

ATTR = {"STACKSIZE": 0x80001009, "DEFAULTTOOL": 0x8000100a, "TOOLTYPE": 0x8000100b,
        "TYPE": 0x8000100f, "FRAMELESS": 0x80001010}
WBTYPE = {"disk": 1, "drawer": 2, "tool": 3, "project": 4}

def chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("dest")
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--stack", type=int, default=8388608)
    ap.add_argument("--type", default="tool", choices=sorted(WBTYPE))
    ap.add_argument("--default-tool")
    ap.add_argument("--tooltype", action="append", default=[])
    a = ap.parse_args()

    tmp = Path(a.dest).with_suffix(".scaled.png")
    subprocess.run(["sips", "-s", "format", "png", "-z", str(a.size), str(a.size),
                    a.src, "--out", str(tmp)], check=True, capture_output=True)
    png = tmp.read_bytes(); tmp.unlink()
    assert png[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"

    body = struct.pack(">II", ATTR["FRAMELESS"], 1)
    body += struct.pack(">II", ATTR["STACKSIZE"], a.stack)
    body += struct.pack(">II", ATTR["TYPE"], WBTYPE[a.type])
    if a.default_tool:
        body += struct.pack(">I", ATTR["DEFAULTTOOL"]) + a.default_tool.encode() + b"\0"
    for tt in a.tooltype:
        body += struct.pack(">I", ATTR["TOOLTYPE"]) + tt.encode() + b"\0"

    # Insert before IEND: walk the chunks.
    pos, out = 8, png[:8]
    while pos < len(png):
        length = struct.unpack(">I", png[pos:pos + 4])[0]
        kind = png[pos + 4:pos + 8]
        end = pos + 12 + length
        if kind == b"IEND":
            out += chunk(b"icOn", body)
        out += png[pos:end]
        pos = end
    Path(a.dest).write_bytes(out)
    print(f"{a.dest}: {a.size}x{a.size} PNG icon, type {a.type}, stack {a.stack}, {len(out)} bytes")

if __name__ == "__main__":
    main()
