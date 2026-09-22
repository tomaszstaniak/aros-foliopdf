# Changelog

## 0.3.7 (2026-09-22)

- Folio remembers where each document was left: page, place on the page,
  zoom and horizontal offset come back when it is opened again. Kept in
  `ENV:Folio/state` while running and `ENVARC:Folio/state` at exit; the
  PDF itself is not touched.
- Project menu: Open Recent, the last five documents, newest first, with
  Clear History. A cleared or dropped entry keeps its reading place.
- Started without a document, from the icon or a bare Shell command, the
  window opens on a welcome page with Open PDF... and the recent list
  instead of a file requester. Navigation, search, zoom and saving are
  disabled until a document is open.

## 0.3.6 (2026-09-22)

- The source file and build script carry the program's name: `src/folio.c`,
  `scripts/build-folio.sh`, output `build/<target>/Folio`.

- Render requests have a state: pending, waiting to retry, or failed. A
  failed page or thumbnail is retried after five seconds without a scroll
  or redraw, three times, then reported in the label; Amiga+R tries the
  failed pages again. The previous image stays on screen throughout. A
  request is the full geometry (size and band), so a change of view, zoom
  or document drops a stale one. `-t` shows the pending, retry and failed
  counts. The queue logic is in `src/renderq.h` with a host-side test,
  `scripts/test-queue.sh`.
- Started from Workbench the program no longer opens a console window:
  the C runtime's `CON:` window is disabled and the standard streams go to
  `NIL:`, MuPDF's warnings and errors are routed through callbacks, and
  the errors that need an answer (context, open, memory) use a requester.
  From a Shell the messages stay on the inherited console.

## 0.3.5 (2026-09-22)

- Zoom anchors on the page and point under the pointer (or the view's
  centre), so zooming in and back out returns to exactly the same spot;
  a column fraction drifted because padding and spacing do not scale.
- A failed page render keeps the previous image and is not retried for
  five seconds; the request for a render is derived from the current
  position on every check, so a position left before its render ran no
  longer renders later.
- `-t` shows renders and timer state next to any status message.

## 0.3.4 (2026-09-22)

- The view's background is drawn into the off-screen buffer before the
  pages, so nothing from the previous frame survives in the gaps between
  pages or on the margins after zooming out.
- `-t` also shows the number of renders and whether the render timer is
  running.

## 0.3.3 (2026-09-22)

- The render queue no longer re-requests a page whose top edge is in view
  on every tick (the visible band was not clipped to the page), which kept
  the view repainting; updates no longer repaint the background first.

## 0.3.2 (2026-09-22)

- No flicker while pages render in the background (0.3.1 redrew the whole
  view with a clear after each page).

## 0.3.1 (2026-09-22)

- Pages no longer stay blank after fast scrolling or zooming: the render
  queue is refreshed from the scroll position itself, a page on screen is
  never dropped from the cache, and tall pages at high zoom render only the
  band around the view.

## 0.3 (2026-09-22)

- Links in pages are underlined and followed on a click: within the
  document to the target position, URLs through openurl.library when it
  is installed.
- The outline jumps to the entry's position on the page, not just the page.

- Ctrl or Amiga with the mouse wheel zooms in and out, around the pointer;
  the keys and menu zoom around the view's centre.
- A zoom shows the page scaled from its previous render at once; the sharp
  render follows. The page nearest the middle of the view renders first.

## 0.2.1 (2026-09-22)

- Selection drawn during a drag no longer spills below the page view.

## 0.2 (2026-09-22)

- The page column no longer loses its scroller on long documents: MUI
  layout sizes are 16-bit, and sixty A4 pages at screen width exceeded
  them. Pages are now drawn by one view-sized object with 32-bit offsets
  and its own scrollbars.
- Pages and thumbnails render on an idle timer instead of inside the draw,
  so fast scrolling no longer stalls and overshoots.
- Sidebar and page column keep their proportions across relayouts.
- Zooming no longer resizes the window.
- Release archive is LHA.

## 0.1 (2026-09-22)

First public test release, for AROS One 1.3 x86_64 (ABIv11).

- MuPDF 1.28.4 cross-built for AROS with two patches: an `OS=AROS` block in
  `Makerules` (no host probes, no `--gc-sections`, no full strip) and a
  `timegm()` replacement for `pdf_parse_date()`.
- Reader: continuous scrolling, thumbnail and outline sidebar with a
  draggable divider, zoom, search, selection and copy (text and image),
  highlight annotations with incremental save, file requester, icon drop,
  Workbench start, keyboard control.

Known limits are listed in `packaging/README`.
