# Building Folio

## What you need

- An AROS x86_64 cross toolchain: `x86_64-aros-gcc`, `-g++`, `-ar`, `-strip`
  and the matching SDK (`Developer/` with `include/` and `lib/`). Release 0.1
  was built with GCC 10.5.0 against the ABIv11 SDK that ships with AROS One
  1.3. **The toolchain, the SDK and the machine you run on must be the same
  ABI**: an ABIv11 binary does not start on mainline (ABIv1) and vice versa,
  and the failure looks like a crash in `AllocMem`, not like a mismatch.
- GNU make 4 (macOS's `/usr/bin/make` is 3.81; use `gmake`), bash, python3,
  git, zip.

Copy `local.env.example` to `local.env` and set `AROS_GCC_ROOT` and
`AROS_SDK`. `scripts/env.sh` reads it; environment variables override it.
`AROS_TARGET` selects `one` (ABIv11, the default) or `mainline`; outputs go
to `build/<target>/` so the two never mix.

## Steps

```
scripts/bootstrap.sh
```

Clones MuPDF at the revision in `upstreams.json` (with its `thirdparty/`
submodules) into `upstream/mupdf`, and creates `work/mupdf` with the patch
series from `patches/mupdf/series` applied. `upstream/` is never edited.
Running it again verifies that `work/` still matches pin + series by content.

```
scripts/build-mupdf.sh
```

Runs MuPDF's own GNU make build in `work/mupdf` with `OS=AROS` and the cross
tools, producing `build/<target>/mupdf/libmupdf.a`, `libmupdf-third.a` and
`mutool`. The configuration is deliberately small:

- `USE_SYSTEM_LIBS=no`: the bundled FreeType, libjpeg, lcms2, zlib, jbig2dec
  and OpenJPEG are compiled in. The AROS SDK's `libz.a`, `libjpeg.a` and
  `libfreetype2.a` are link stubs for shared libraries, so linking against
  them would add runtime dependencies that a user's system may not have.
- `html=no xps=no svg=no extract=no brotli=no mujs=no`, no Tesseract, barcode,
  curl or libcrypto. PDF reading and annotation do not need them; each can be
  re-enabled at the cost of more patches and a bigger binary.
- No viewer (`platform/gl`, `platform/x11`): Folio is the viewer.

```
scripts/build-folio.sh
```

The render request queue (`src/renderq.h`) has a test that runs on the host
with the system compiler, `scripts/test-queue.sh`.

Compiles `src/folio.c` against the headers in `work/mupdf/include` and links
`-lmupdf -lmupdf-third -lpthread -lm` (lcms2 uses pthread mutexes). Output:
`build/<target>/Folio`, not stripped.

```
scripts/make-release.sh
```

Stages `dist/Folio-<ver>.x86_64-aros-v11.lha` (needs `LHA_WRITER`, a
create-capable classic lha; binary stripped with
`--strip-unneeded --remove-section .comment`, icon, README, licence texts,
SHA256SUMS, embedded `.arospkg/manifest.toml`) and `dist/Folio-<ver>-source.zip`
(this repository; MuPDF is pinned by commit and reconstructed by
`scripts/bootstrap.sh`). Only `AROS_TARGET=one` is accepted, because that is the only target
the release has been tested on.

## AROS specifics worth knowing

- **Never fully strip an AROS x86_64 executable.** `x86_64-aros-strip` with no
  options produces a file that loads without its `.text` relocations applied
  and dies in the first `OpenLibrary()`. `--strip-unneeded` is safe. Patch
  0001 removes `-Wl,-s` from MuPDF's release link for the same reason.
- **`--gc-sections` is rejected** by the AROS linker for executables
  (relocatable objects). Patch 0001 removes it.
- **`timegm()` is missing** from AROS's posixc. Patch 0002 supplies a
  replacement for `pdf_parse_date()`; it was checked against a host `timegm`
  for 19 201 dates between 1902 and 2301.
- **Stack.** The program sets `__stack` to 8 MB, which the Shell honours. A
  Workbench start ignores it and uses the icon's stack size; the icon made
  by `scripts/mkpngicon.py` carries 8 MB. With the default 40 KB, FreeType
  overflows the stack on the first page.
- **Fonts.** The URW base35 fonts are compiled into the binary from MuPDF's
  `resources/`; the build regenerates their `generated/*.c` sources with a
  bash script, so no host-compiled helper is needed.
- The build rewrites 16 tracked files under `work/mupdf/generated/` with
  byte-equivalent content, which leaves `work/` dirty. `git -C work/mupdf
  checkout -- generated` restores them; do not commit them into a patch.

## Patch workflow

`upstream/mupdf` is a clean checkout, `work/mupdf` the editable copy. Edit
in `work/`, stage with `git -C work/mupdf add`, then

```
scripts/save-patch.sh 0003-short-name --problem "..." --solution "..." --scope "..."
```

writes `patches/mupdf/0003-short-name.patch` with a header (problem,
solution, scope, base commit, upstream status) and appends it to `series`.
`scripts/check-reproduction.sh` applies the whole series to the pinned base
in a temporary directory. Changes inside `thirdparty/` submodules are not
covered by the in-sync check; keep them out of the series or handle the
submodule separately.

## Icon

```
scripts/mkpngicon.py assets/source/folio-icon.png packaging/Folio.info --size 64
```

writes an AROS PNG icon: the scaled PNG with an `icOn` chunk (type, stack
size, optional default tool and tooltypes) before `IEND`, the format
`icon.library` reads and writes.
