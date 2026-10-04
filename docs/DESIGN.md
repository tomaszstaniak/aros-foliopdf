# Folio internals

`src/folio.c` contains the Zune application and calls a statically linked
MuPDF. Three headers isolate logic that can be tested without AROS:

| File | Responsibility | Host test |
| --- | --- | --- |
| `src/layout.h` | Page rectangles, spreads, fitting and navigation | `scripts/test-layout.sh` |
| `src/renderq.h` | Render requests, retries and failure limits | `scripts/test-queue.sh` |
| `src/pageinput.h` | Page-number parsing and bounds checks | `scripts/test-pageinput.sh` |

## Window and layout

The window contains navigation controls, a Find field, a Pages/Outline
sidebar, a Balance divider and the document area. Thumbnails are `Cell`
objects derived from MUI Area. The document area is a single `Strip` object
with separate horizontal and vertical scrollbars.

Pages are drawn inside the strip rather than laid out as MUI objects.
MUI's 16-bit layout sizes cannot represent a long document at reading
scale. The strip stores page positions and scroll offsets in 32-bit values
and drives the scrollbars through `MUIA_Prop_*`.

`src/layout.h` calculates rectangles shared by drawing, rendering, hit
testing, zoom anchoring and navigation:

- Single Page lays out a continuous column.
- Magazine pairs pages 1–2, 3–4, etc. Each partner gets half the available
  width minus the gap; the pair is top-aligned. An odd final page gets the
  full width.
- Magazine limits drawing and scrolling to the active spread. When zoomed
  in, scrolling pans within it; page navigation changes spreads.
- Fit Page uses both viewport dimensions. Fit Width uses the width.
  Magazine refits after a spread change or resize until manual zoom.

Wheel zoom preserves the page point under the pointer. Menu and keyboard
zoom use the view centre. The horizontal scrollbar stays in the layout
because hiding it caused Zune to resize the window. The sidebar uses a
fixed minimum width and zero layout weight; the divider adjusts its share.

Scrollbar handlers consume the notification's `MUIV_TriggerValue` directly.
Reading `MUIA_Prop_First` back can round a one-pixel step to zero through the
native gadget's 16-bit position. This was reproduced with a 127-page PDF
on AROS One 1.3 x86_64 ABIv11 on 2026-09-28.

## Rendering

A draw requests missing page images. A 50 ms timer renders after the view
has been still for 150 ms, processing one visible cell per tick, document
pages before thumbnails. Rendering runs on the UI task and can block input.

MuPDF produces RGB pixmaps, drawn with `WritePixelArray(..., RECTFMT_RGB)`.
The cache holds up to six document-page images and 300 thumbnails, evicting
the least recently drawn. A cell composes its background, page and overlays
in an off-screen bitmap, then blits once. Old images remain visible until
replacement renders finish.

Render requests carry page geometry and the required row band. The timer
recalculates these before rendering, replacing stale requests. Failures
retry after five seconds, up to three attempts. After that, the status
label reports the failure; Amiga+R resets failed requests.

## Navigation and input

Explicit jumps preserve the requested page as current until the scroll
offset changes. Otherwise, Single Page selects the page one third of the
way down the view. Magazine selects the page with the most visible area,
preserving the current page on a tie.

The page-entry field accepts one-based decimal numbers. `src/pageinput.h`
checks the entire input, bounds and overflow. Return calls `go_to()` and
restores the actual page number. Invalid input leaves the view unchanged.
Status updates do not overwrite an active edit. Amiga+J clears and focuses
the field. Current-page changes redraw thumbnail selection without moving
the sidebar's scroll position.

The strip handles raw keyboard and mouse events, including drag scrolling.
It also handles selected menu shortcuts, including Amiga+J: Zune sets
`WFLG_RMBTRAP` over an object with a context menu, preventing Intuition from
delivering those shortcuts through the menu. Text-entry keys are left to
the active field.

## Text, search and links

Selection stores an anchor and endpoint in page coordinates, so zoom and
layout changes preserve it. `fz_highlight_selection` supplies quads for the
selected pages; `COMPLEMENT` draws and erases them. Dragging beyond the
viewport scrolls on a timer. Extracted text is cached for up to eight pages.

Copy joins selected text across pages and writes an IFF FTXT clipboard:
CHRS contains Latin-1 with substitutions; UTF8 contains the Unicode text.
Copy as Image renders a rectangle on one page at 144 dpi and writes an
uncompressed 24-bit ILBM.

Search extracts text page by page, wrapping once, and frames hits on the
first matching page. Find Next starts after that page. It does not step
through each hit within a page.

Links are cached with page text and underlined. A press and release within
three pixels follows the link. Internal links resolve to page coordinates;
external links open through dynamically loaded `openurl.library`. Outline
entries store page and vertical target coordinates and use `go_to_position()`.

## Highlights and saving

Highlight creates a `PDF_ANNOT_HIGHLIGHT` on each selected page, marks the
document modified and invalidates those page images. Save uses incremental
PDF saving for the current file. If that is unavailable, it asks the user
through the status label to use Save As; it does not overwrite in place.
Save As writes a full copy. Opening another document or quitting prompts
before discarding unsaved changes.

## Documents and persistence

File requesters, Workbench project arguments and AppWindow drops use the
same document-loading path. Opening a replacement resets selection, search,
caches and the outline. A failed open keeps the previous document.

`DocState` records store the path, page, page-space offset, zoom, horizontal
offset, view mode and recent-file order. They are written to `ENV:Folio/state`
on document switches and to `ENVARC:Folio/state` on exit. Restore runs after
layout exists. Magazine fit mode refits to the new window dimensions.

The start page shows the latest document with a preview and up to four
other recent documents. Preview images are 120×160 PPM files under
`ENV:Folio/thumbs/`, persisted to `ENVARC:` on exit. The start page reads
these files without reopening PDFs. Paths are resolved to full paths;
older relative records are merged on load. Clear History removes entries
from the recent list without deleting their reading positions.

A Workbench launch sets `__nostdiowin` and redirects stdout/stderr to NIL:.
A Shell launch writes MuPDF diagnostics to stderr. Errors requiring user
action use requesters in either case.

## Current limits

Rendering has no background worker. There is no printing, form filling,
OCR, double-click word selection, highlight removal or support for creating
other annotation types. See the [user guide](../packaging/README) for
clipboard compatibility limits.
