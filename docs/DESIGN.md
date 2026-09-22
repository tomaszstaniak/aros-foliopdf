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

## Cells

Thumbnails and pages are the same MUI area subclass, `Cell`, in two
"columns" (`KIND_THUMB`, `KIND_MAIN`) that differ in width, padding, label
and cache size. A cell knows its page's aspect ratio and renders lazily on
first draw with `fz_new_pixmap_from_page` (RGB, no alpha), drawing the
pixmap with `WritePixelArray(..., RECTFMT_RGB)`: MuPDF's RGB layout matches
cybergraphics' byte for byte, so nothing is converted. Each column keeps a
bounded number of rendered pages (6 in the page column, 300 thumbnails) and
drops the one drawn longest ago.

MUI has no height-for-width layout. Cells report their heights for the
column's assumed width; a cell that finds itself drawn at another width
queues one relayout through `MUIM_Application_PushMethod`, which changes
the assumed width and re-runs the layout inside a
`MUIM_Group_InitChange/ExitChange` bracket. That is how the sidebar follows
the divider and the window, and how zoom works: at zoom > 1 the page cells
ask for `view width × zoom` as their minimum width, the virtual group grows
wider than the view, and the scrollgroup adds a horizontal scroller. The
bracket must enclose the **scrollgroup**, not the inner group, because
Zune re-lays out only the bracketed object and the scroller is decided in
the scrollgroup's layout hook.

Each cell composes into an off-screen bitmap and blits once, so background,
page image, selection and outline never appear as separate steps. A page
being re-rendered (after a highlight, for instance) keeps its old image on
screen until the new one replaces it in a single blit.

## Current page

Zune's scrollgroup moves its contents with `MUIA_NoNotify`, so there is no
scroll notification. Instead, the redraw of any page cell queues one check
that reads `MUIA_Virtgroup_Top` and picks the page a third of the way down
the view. After an explicit jump (thumbnail, outline, Next, Home/End) the
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

## What is not there yet

Rendering runs on the UI task, so a slow page blocks the window while it
draws. No links, forms, printing, other annotation types, or removal of
highlights. Selection cannot start on a double click. The outline jumps to
a page, not to a position within it.
