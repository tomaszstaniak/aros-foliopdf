# Building Folio

## Requirements

- An AROS x86_64 cross toolchain: `x86_64-aros-gcc`, `-g++`, `-ar`, `-strip`
  and a matching SDK (`Developer/` with `include/` and `lib/`). The toolchain,
  SDK and target system must use the same ABI. ABIv11 and mainline ABIv1
  binaries are not interchangeable.
- GNU make 4 (macOS's `/usr/bin/make` is 3.81; use `gmake`), bash, python3,
  git, zip.

Create the local configuration:

```sh
cp local.env.example local.env
```

Set `AROS_GCC_ROOT` to the directory containing the cross tools and
`AROS_SDK` to the matching SDK in `local.env`. `scripts/env.sh` reads it; environment variables override it.
`AROS_TARGET` selects `one` (ABIv11, the default) or `mainline`; outputs go
to `build/<target>/`. These names select directories and defaults, not a
compiler architecture. The standard scripts default to x86_64 tools.
The separate i386 and Raspberry Pi development packages include their own
cross-build recipe and report in the accompanying source archive.

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
`mutool`. The build configuration is:

- `USE_SYSTEM_LIBS=no`: bundled FreeType, libjpeg, lcms2, zlib, jbig2dec
  and OpenJPEG are linked statically.
- `html=no xps=no svg=no extract=no brotli=no mujs=no`, no Tesseract, barcode,
  curl or libcrypto. PDF reading and annotation do not need them; each can be
  re-enabled only after checking their build and runtime dependencies.
- No viewer (`platform/gl`, `platform/x11`): Folio is the viewer.

```
scripts/build-folio.sh
```

Compiles `src/folio.c` against the headers in `work/mupdf/include` and links
`-lmupdf -lmupdf-third -lpthread -lm` (lcms2 uses pthread mutexes). Output:
`build/<target>/Folio`, not stripped.

## Tests

Run the host tests with a system C compiler:

```sh
sh scripts/test-queue.sh
sh scripts/test-layout.sh
sh scripts/test-pageinput.sh
```

These cover render request states, page/spread geometry and page-number
validation. They do not exercise Zune, PDF rendering or clipboard receivers.
Test the target executable on the matching AROS system before publishing.

## Packaging

```sh
LHA_WRITER=/path/to/lha scripts/make-release.sh
```

Stages `dist/Folio-<ver>.x86_64-aros-v11.lha` (needs `LHA_WRITER`, a
create-capable classic lha; binary stripped with
`--strip-unneeded --remove-section .comment`, icon, README, licence texts,
SHA256SUMS, embedded `.arospkg/manifest.toml`) and `dist/Folio-<ver>-source.zip`
(this repository; MuPDF is pinned by commit and reconstructed by
`scripts/bootstrap.sh`). Only `AROS_TARGET=one` is accepted, because that is the only target
the release script supports.

The source ZIP comes from `git archive HEAD`; it excludes uncommitted
changes. Commit the source and documentation used for the binary before
creating a release. The script stages archives but does not publish them.

## Porting constraints

- **Never fully strip an AROS x86_64 executable.** `x86_64-aros-strip` with no
  options produces a file that loads without its `.text` relocations applied
  and dies in the first `OpenLibrary()`. `--strip-unneeded` is safe. Patch
  0001 removes `-Wl,-s` from MuPDF's release link for the same reason.
- **`--gc-sections` is rejected** by the AROS linker for executables
  (relocatable objects). Patch 0001 removes it.
- **PDF dates.** Patch 0002 supplies a local `timegm()` replacement
  for `pdf_parse_date()`. The patch header records its validation.
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

On macOS, this uses `sips` to write an AROS PNG icon: the scaled PNG with an `icOn` chunk (type, stack
size, optional default tool and tooltypes) before `IEND`, the format
`icon.library` reads and writes.
