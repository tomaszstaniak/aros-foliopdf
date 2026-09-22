# How Folio is put together

One C file, `src/pdfman.c`, on top of a statically linked `libmupdf`.

## Window

A Zune application with one window: a row with Previous / page label / Next,
a Find field, and below it a horizontal group of the sidebar, a Balance
divider, and the page column.

The **sidebar** is a Register with two pages: "Pages", a scrolling virtual
group of thumbnails, and "Outline", a Listview filled from `fz_load_outline`
(flattened, indented by depth; a click jumps to the entry's page).

The **page column** is a scrolling virtual group with one cell per page,
stacked vertically; that is what makes scrolling continuous.

## Cells and the page column

Thumbnails are `Cell` objects, an MUI area subclass, in a scrolling virtual
group. Pages are **not** MUI objects: MUI layout sizes are 16-bit, and sixty
A4 pages at screen width already exceed 32767 px, at which point a virtual
group silently loses its scroller. The page column is one `Strip` object the
size of the view. It keeps the scroll offsets and each page's position in
32-bit values, paints the visible pages itself, and drives two `Scrollbar`
objects (32-bit ranges) through `MUIA_Prop_*`. Both kinds share one
`CellData` record per page and the same painting code; a page cell learns
its placement from the strip at each draw, a thumbnail from its object.

A cell renders lazily: it draws a blank page and asks for an image
(`fz_new_pixmap_from_page`, RGB, no alpha), which a 50 ms timer produces
once the view has been still for 150 ms, one visible cell per tick, pages
before thumbnails. Rendering inside the draw method stalled scrolling. The
pixmap is drawn with `WritePixelArray(..., RECTFMT_RGB)`: MuPDF's RGB
layout matches cybergraphics' byte for byte. Each column keeps a bounded
number of rendered pages (6 in the page column, 300 thumbnails) and drops
the one drawn longest ago.

Zoom multiplies the column width; the strip re-lays out its pages and
updates the scrollbars. The horizontal scrollbar stays in the layout even
when the column fits: hiding and showing it makes Zune recalculate the
window, which snaps it back to its remembered size.

The sidebar has weight 0 and a fixed minimum width (a zero-height spacer),
so the page column takes the rest and the divider redistributes from there.
Sharing space by weight between the sidebar and the column left the column
at its minimum after every relayout.

Each cell composes into an off-screen bitmap and blits once, so background,
page image, selection and outline never appear as separate steps. A page
being re-rendered keeps its old image on screen until the new one replaces
it in a single blit.

## Current page

The redraw of the strip queues one check that picks the page a third of the
way down the view from the scroll offset. After an explicit jump (thumbnail, outline, Next, Home/End) the
requested page is kept until the offset changes, because the last pages
cannot scroll to the top of the view.

## Input

Keyboard and mouse arrive through a window event handler hosted by the first
page cell (`MUIM_HandleEvent`, `IDCMP_RAWKEY | MOUSEBUTTONS`, plus
`MOUSEMOVE` while dragging). Menu shortcuts for Copy, Copy as Image,
Highlight, Save, Save As and the zoom keys are handled there as well: while
the pointer is over an object with a context menu, Zune sets `WFLG_RMBTRAP`
and Intuition stops delivering menu shortcuts.

## Selection

A selection is an anchor and an end, each a page plus a point in that page's
coordinate space, so a relayout or a zoom leaves it in place. For each page
in the range, `fz_highlight_selection` gives the quads (whole page in the
middle, to the page corner on the first and last), which are inverted over
the page image with `COMPLEMENT`. During a drag nothing is redrawn: the old
quads are inverted again (undo) and the new ones inverted, so only the
difference changes on screen. Dragging past the view's edge scrolls it on a
timer. Extracted page text (`fz_new_stext_page_from_page`) is cached for up
to eight pages.

**Copy** joins `fz_copy_selection` of every page with line feeds and writes
IFF `FTXT` to the clipboard with two chunks: `CHRS`, the text reduced to
Latin-1 with ASCII stand-ins for common typographic characters, which every
AROS program can paste; and `UTF8`, the text unchanged, the convention used
by other AROS ports for full Unicode.

**Copy as Image** renders the rectangle between the drag's end points (one
page) at 144 dpi with a draw device and writes an uncompressed 24-bit
`ILBM` (`BMHD` + `BODY`, planes red 0..7, green, blue) to the clipboard.

**Highlight** creates one `PDF_ANNOT_HIGHLIGHT` per page from the same quads
(`pdf_create_annot`, `pdf_set_annot_quad_points`, `pdf_set_annot_color`,
`pdf_update_annot`), marks the document modified, and re-renders those
pages. **Save** uses `pdf_save_document` with `do_incremental` when the file
allows it, appending the change and leaving the original bytes; **Save As**
writes a full copy. Open, icon drop and Quit ask before discarding unsaved
highlights.

## Search

The Find field (Return or `Amiga+G`) extracts text page by page from the
current page on, wrapping once, and stops at the first page with a hit
(`fz_search_stext_page`); the hits are framed on that page.

## Opening

`load_document()` tears down selection, caches, search state and both
columns, opens the new document (the old one stays if that fails, with a
requester), rebuilds the columns inside a change bracket, and refills the
outline. The window is a Workbench AppWindow; Zune sets `MUIA_AppMessage`
on the root object for a drop, which a notification forwards to the same
path. With `argc == 0` the program was started from Workbench and `argv` is
the `WBStartup`; its first project argument, if any, is opened.

## Links

A page's links (`fz_load_links`) are cached alongside its text and drawn as
an underline. A press that is released without moving more than three
pixels is a click; a link under it is followed: internal targets through
`fz_resolve_link` to a page and position, external ones by opening
`openurl.library` at that moment (it is optional on a system, so it is not
linked in). The outline uses the same jump-to-position.

## What is not there yet

Rendering runs on the UI task, so a slow page blocks the window while it
draws. No forms, printing, other annotation types, or removal of
highlights. Selection cannot start on a double click. The outline jumps to
a page, not to a position within it.
