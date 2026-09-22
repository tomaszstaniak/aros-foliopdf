# Folio

A PDF reader for AROS, built on [MuPDF](https://mupdf.com/) with an
Intuition/Zune (MUI) front end.

- Continuous vertical scrolling; pages of different sizes in one column
- Sidebar with page thumbnails and the document outline
- Zoom in/out, fit width, fit page
- Text search
- Text selection across pages; copy as text (FTXT with CHRS and UTF8
  chunks) or as a 24-bit ILBM image
- Yellow highlight annotations, saved into the PDF incrementally
- Links: internal ones jump to their target, URLs open through openurl.library
- Open through a file requester or by dropping a file icon on the window;
  Workbench and Shell start
- Keyboard navigation

Current release: **0.3.5**, for AROS One 1.3 on x86_64 (ABIv11). The binary
does not run on mainline AROS (ABIv1); see [docs/BUILDING.md](docs/BUILDING.md)
for building against another SDK.

## Using it

```
Folio [-t] [file.pdf [page]]
```

Without a file, a requester opens. `-t` shows page render times and the
render queue's state in the label. Started from Workbench there is no
console window; from a Shell, warnings and errors go to its console. Menus, keys and mouse are listed in [packaging/README](packaging/README),
which ships in the release archive.

## Building

Needs an AROS x86_64 cross toolchain (GCC 10.5 was used), GNU make 4, bash,
python3, and git. In short:

```
cp local.env.example local.env      # point AROS_GCC_ROOT/AROS_SDK at your toolchain
scripts/bootstrap.sh                # pinned MuPDF checkout + patched work copy
scripts/build-mupdf.sh              # libmupdf and mutool
scripts/build-pdfman.sh             # the reader -> build/one/pdfman
scripts/test-queue.sh               # host-side test of the render queue
scripts/make-release.sh             # release archives in dist/
```

Details, the MuPDF configuration and the reasons behind the two patches are
in [docs/BUILDING.md](docs/BUILDING.md). How the program is put together:
[docs/DESIGN.md](docs/DESIGN.md).

## Layout

```
src/pdfman.c        the reader
src/renderq.h       render request queue, shared with tests/queue_test.c
upstreams.json      pinned MuPDF revision
patches/mupdf/      changes to MuPDF, in series order
scripts/            bootstrap, build, patch and release scripts
tests/fixtures/     generated test PDFs and their generators
packaging/          icon, release README, embedded package manifest, licence texts
assets/source/      icon artwork
docs/               building and design notes
```

`upstream/`, `work/`, `build/` and `dist/` are created by the scripts and
are not tracked.

## Licence

Folio is free software under the GNU Affero General Public License, version 3
or later ([LICENSE](LICENSE)). MuPDF is Copyright Artifex Software, Inc.,
under the same licence; the notices of the libraries and fonts linked into
the binary are in [packaging/licenses](packaging/licenses). A binary is
distributed together with its complete corresponding source: this repository
at the release tag, which pins the MuPDF revision and carries the patches.

Copyright (C) 2026 Tomasz Staniak.
