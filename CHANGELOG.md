# Changelog

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
