# Folio

Folio is a PDF reader for AROS. It uses MuPDF for rendering and Zune (MUI)
for the interface.

It supports continuous scrolling, two-page spreads, thumbnails, a document
outline, text search, selection and copying. You can add yellow highlights
and save them in the PDF. Folio remembers the reading position, zoom and
view mode for each document.

## Install and open a PDF

Use a package that matches your AROS architecture and ABI. Unpack it into
any drawer and double-click the Folio icon. Keep the supplied icon: it sets
the required 8 MB stack.

Open a PDF with **Project > Open**, drop its icon on the window, or select
one of the recent documents on the start page.

From a Shell:

```text
Stack 8388608
Folio "Work:Documents/example.pdf" 12
```

The optional number is the starting page, counted from 1. Without a filename,
Folio opens the start page. `Folio -t file.pdf` displays rendering diagnostics;
`-l file` writes them to a log.

## Read and navigate

- **Single Page** (`Amiga+1`): continuous vertical scrolling.
- **Magazine** (`Amiga+2`): pairs 1–2, 3–4, and so on. An odd final page is
  shown alone. Selecting this mode fits each spread to the window.
- **Previous / Next**: move by one page or, in Magazine, one spread.
- **Go to Page** (`Amiga+J`): enter a page number and press Return. You can
  also edit the number between Previous and Next directly.
- **Pages / Outline**: click a thumbnail or outline entry to jump. The Pages
  panel keeps its scroll position while you read.
- **Fit Width / Fit Page** (`Amiga+0` / `Amiga+9`): fit the page or spread.
  Ctrl+wheel zooms around the pointer.

Drag over text to select it. Use **Copy** (`Amiga+C`) or **Highlight**
(`Amiga+H`), then **Save** (`Amiga+S`) to save highlights. If the PDF cannot
be updated in place, use **Save As** (`Amiga+A`).

The [user guide](packaging/README) lists all shortcuts, search behaviour,
clipboard formats and saved settings. It is also included in binary packages.

## Limitations

- No printing, form filling, OCR or annotation types other than highlights.
  Existing highlights cannot be removed in Folio.
- Pages render in a background thread, so the window stays usable, but a
  complex page takes as long to appear as before. Pages with unsaved
  highlights are rendered by the window itself until saved.
- Search and text selection need a PDF text layer. Scanned images without
  one can be viewed but not searched as text.
- Copy as Image writes a 24-bit ILBM. It appeared blank in MultiView during
  AROS One testing; compatibility with other receivers is unverified.

## Builds

The release packaging script targets **x86_64 ABIv11 (AROS One)**. Version **0.5** (candidate) renders pages in a background worker; see [CHANGELOG.md](CHANGELOG.md).

On 2026-10-12 the worker was measured on AROS One x86_64 ABIv11 under
QEMU and run on i386 ABIv0. The Raspberry Pi aarch64 build compiled and
linked; it was not run.

See [Building Folio](docs/BUILDING.md) for dependencies, commands, tests and
packaging. [Design](docs/DESIGN.md) describes the code and rendering model.

## Source layout

| Path | Contents |
| --- | --- |
| `src/folio.c` | Application, UI, rendering and PDF operations |
| `src/layout.h` | Page and spread geometry |
| `src/renderq.h` | Render request states, jobs and retries |
| `src/renderworker.h` | Background render thread |
| `src/pageinput.h` | Page-number validation |
| `tests/` | Host tests and PDF fixtures |
| `scripts/` | Build, test and packaging tools |
| `upstreams.json`, `patches/mupdf/` | MuPDF revision and local patches |
| `packaging/` | User guide, icon, manifest and dependency licences |
| `assets/source/` | Original icon artwork |

## Licence

Folio is licensed under the [GNU AGPL version 3 or later](LICENSE).
MuPDF is copyright Artifex Software, Inc. Dependency and font licences are
in [packaging/licenses](packaging/licenses).

Copyright (C) 2026 Tomasz Staniak.
