# Changelog

## Unreleased

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
