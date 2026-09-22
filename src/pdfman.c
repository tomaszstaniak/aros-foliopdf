/*
 * Folio - PDF reader for AROS on libmupdf, Intuition + Zune front end.
 *
 * Copyright (C) 2026 Tomasz Staniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One window: a sidebar with a thumbnail of every page and a continuously
 * scrolling column of pages. Both columns are made of the same Cell class.
 * Rendering happens on the UI task; see docs/DESIGN.md.
 *
 *   pdfman [-t] [file.pdf [page]]   no file: a file requester opens
 *
 * Started from Workbench (argc == 0, argv is the WBStartup): the first
 * project icon passed becomes the document, otherwise the requester opens.
 * Project/Open... (Amiga+O) opens another file; so does dropping a file's
 * icon onto the window (the window is a Workbench AppWindow).
 *                                   -t: show page render times in the label
 *
 * Mouse: drag over the pages to select text, across pages; the view scrolls
 * when the pointer leaves it. Amiga+C or Edit/Copy puts it on the clipboard.
 * Right button over a page: a context menu with Copy and Copy as Image. The
 * latter renders the rectangle between the drag's end points (one page) at
 * 144 dpi and puts it on the clipboard as a 24-bit ILBM.
 * Highlight (Amiga+H, Edit menu, context menu) turns the selection into a
 * yellow highlight annotation. Save (Amiga+S) appends the changes to the
 * file incrementally; Save As... writes a new file. Closing with unsaved
 * changes asks first.
 * The sidebar has two pages: thumbnails, and the document's outline (table
 * of contents) as a list; a click on an entry jumps to its page.
 * Zoom: View menu, Amiga+= / Amiga+- (also keypad + and -) in 25 % steps,
 * Amiga+0 fits the width again, Amiga+9 fits the whole current page.
 * Search: Amiga+F puts the cursor in the search field; Return or Amiga+G
 * finds the next page with a hit, starting after the current one.
 * Keys: cursor up/down scroll, PgUp/PgDn and Space by a screenful, Home/End,
 * cursor left/right previous/next page. The wheel scrolls the column under
 * the pointer.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/asl.h>
#include <proto/iffparse.h>

#include <libraries/mui.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <libraries/iffparse.h>
#include <datatypes/pictureclass.h>
#include <cybergraphx/cybergraphics.h>
#include <devices/rawkeycodes.h>
#include <intuition/classusr.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

/* MuPDF recurses deeply and keeps large buffers on the stack; the Shell
 * default is far too small. AROS startup reads this symbol and swaps stacks,
 * so the program does not depend on the user's `Stack` setting. */
__attribute__((used)) unsigned long __stack = 8UL * 1024 * 1024;

#define MUIM_Reader_Step     0x80440001UL  /* move by msg->delta pages */
#define MUIM_Reader_Goto     0x80440002UL  /* show msg->page (0-based) */
#define MUIM_Reader_Relayout 0x80440003UL  /* column msg->kind is now msg->width wide */
#define MUIM_Reader_Scrolled 0x80440004UL  /* the page column was scrolled */
#define MUIM_Reader_ShowTiming 0x80440005UL /* refresh the label after a timed render */
#define MUIM_Reader_Find     0x80440006UL  /* search for the field's text from the current page on */
#define MUIM_Reader_Drop     0x80440008UL  /* a Workbench AppMessage landed on the window */
#define MUIM_Reader_Zoom     0x80440009UL  /* msg->mode: ZOOM_* */
#define MUIM_Reader_OutlinePick 0x8044000AUL /* the outline list's active entry changed */
#define OUTLINE_MAX 4096
#define ZOOM_STEP 1.25f
#define ZOOM_MIN  0.25f
#define ZOOM_MAX  8.0f
enum { ZOOM_IN, ZOOM_OUT, ZOOM_FITWIDTH, ZOOM_FITPAGE };
struct MUIP_Reader_Zoom { STACKED ULONG MethodID; STACKED LONG mode; };
#define MAX_HITS 64
#define AUTOSCROLL_MS 60
#define MAX_STEXT 8         /* extracted pages kept for selection drawing */
#define MUIA_Cell_Page       0x80440010UL  /* i.. LONG, 0-based page */
#define MUIA_Cell_Kind       0x80440011UL  /* i.. LONG, KIND_* */

enum { KIND_THUMB, KIND_MAIN, KIND_COUNT };

/* Menu items. MUI hands a NewMenu item's UserData back as the return ID. */
enum { MEN_ABOUT = 1, MEN_ABOUTMUI, MEN_OPEN, MEN_SAVE, MEN_SAVEAS, MEN_COPY, MEN_COPYIMAGE,
       MEN_HIGHLIGHT, MEN_FIND, MEN_FINDNEXT, MEN_ZOOMIN, MEN_ZOOMOUT, MEN_FITWIDTH, MEN_FITPAGE };
#define IMAGE_DPI 144

#define ID_FTXT MAKE_ID('F','T','X','T')
#define ID_CHRS MAKE_ID('C','H','R','S')
#define ID_UTF8 MAKE_ID('U','T','F','8')

#define CELL_W_MIN     40
#define CELL_SLACK     3    /* width change, in pixels, that is worth a relayout */
#define SIDEBAR_WEIGHT 22   /* against 100 for the page column */
#define LINE_STEP      48   /* pixels per cursor key press */
#define WHEEL_STEP     96   /* pixels per wheel notch */

struct MUIP_Reader_Step     { STACKED ULONG MethodID; STACKED LONG delta; };
struct MUIP_Reader_Goto     { STACKED ULONG MethodID; STACKED LONG page; };
struct MUIP_Reader_Relayout { STACKED ULONG MethodID; STACKED LONG kind; STACKED LONG width; };

struct CellData
{
    fz_pixmap *pix;         /* rendered on draw for a box of boxW x boxH */
    LONG boxW, boxH;
    LONG page, kind;
    float aspect;           /* page height / width */
    fz_rect box;            /* page bounds in PDF units */
    ULONG stamp;            /* last draw, for eviction */
    struct MUI_EventHandlerNode ehn;    /* used by one cell only, see Cell_Setup */
    BOOL has_handler;
};

/* MUI has no height-for-width layout. Cells report their height for the
 * column's assumed width `w`; a cell that finds its real width differs asks
 * for one relayout with the real width. */
struct Column
{
    Object *group;          /* the virtual group holding the cells */
    Object **cells;         /* one per page */
    LONG w;
    LONG pad;
    LONG spacing;
    BOOL label;             /* page number under the cell */
    BOOL pending;           /* a relayout is queued */
    int limit;              /* rendered cells kept before the oldest is dropped */
    int live;
};

static struct Column cols[KIND_COUNT] = {
    [KIND_THUMB] = { .w = 100, .pad = 6, .spacing = 4, .label = TRUE,  .limit = 300 },
    /* A page at screen width is megabytes of pixels; keep what is on screen
     * plus a little scroll-back. */
    [KIND_MAIN]  = { .w = 600, .pad = 6, .spacing = 4, .label = FALSE, .limit = 6 },
};

static fz_context *ctx;
static fz_document *doc;
static int page_count;
static int current_page;
static ULONG draw_clock;
static BOOL layout_valid;   /* cells have real positions: the window is open */
static BOOL scroll_check_pending;
/* Page column scale: 1.0 is fit-to-width; the column's cell width is the
 * view width times zoom, wider than the view when zoomed in. */
static float zoom = 1.0f;
static LONG view_w;         /* page column view width seen at the last draw */
static BOOL show_timing;    /* -t */
static LONG last_ms, worst_ms;  /* page-column renders only */
static char status_text[64];    /* shown in the label until the page changes */

/* Text selection: an anchor (where the button went down) and a moving end,
 * each a page and a point in that page's space, so a relayout or re-render
 * at another scale leaves the selection where it was. */
static int sel_page = -1, sel_end_page = -1;
static fz_point sel_a, sel_b;
static BOOL selecting;
static LONG autoscroll_dy;      /* while dragging outside the view */
static struct MUI_InputHandlerNode autoscroll_ihn;
static BOOL autoscroll_on;

/* Small cache of extracted page text, used while drawing the selection. */
static struct { int page; fz_stext_page *text; ULONG used; } stext_cache[MAX_STEXT];
static ULONG stext_clock;

/* Search: hits on one page at a time, in page space. */
static Object *search_field;
static int search_page = -1;
static fz_quad search_hits[MAX_HITS];
static int search_n;
static LONG goto_top = -1;  /* offset left by the last explicit jump */
static Object *app_obj, *reader_obj, *page_label, *win_obj, *pages_scroll;
static Object *outline_list, *sidebar;
static int outline_pages[OUTLINE_MAX];  /* page of each list entry */
static int outline_n;
static BOOL outline_quiet;  /* we are setting the active entry ourselves */
static char title[300];
static char label_text[96];
static struct MUI_CustomClass *CellClass, *ReaderClass;
static Object *context_menu;    /* shared by the page cells; disposed by us */

/* --- MuPDF ----------------------------------------------------------------- */

static void update_label(void)
{
    if (status_text[0])
        snprintf(label_text, sizeof(label_text), "\33c%d / %d   %s", current_page + 1, page_count, status_text);
    else if (show_timing)
        snprintf(label_text, sizeof(label_text), "\33c%d / %d   last %ld ms, worst %ld ms",
                 current_page + 1, page_count, (long)last_ms, (long)worst_ms);
    else
        snprintf(label_text, sizeof(label_text), "\33c%d / %d", current_page + 1, page_count);
    if (page_label)
        SET(page_label, MUIA_Text_Contents, (IPTR)label_text);
}

static LONG now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (LONG)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* Render a page scaled to fit w x h. Returns NULL on a MuPDF error. */
static fz_pixmap *render_fit(int number, LONG w, LONG h)
{
    fz_pixmap *pix = NULL;
    fz_page *page = NULL;

    fz_var(pix);
    fz_var(page);

    fz_try(ctx)
    {
        fz_rect box;
        float sx, sy, s;

        page = fz_load_page(ctx, doc, number);
        box = fz_bound_page(ctx, page);
        sx = (float)w / (box.x1 - box.x0);
        sy = (float)h / (box.y1 - box.y0);
        s = sx < sy ? sx : sy;
        /* RGB without alpha is RECTFMT_RGB byte for byte: no conversion
         * between MuPDF and WritePixelArray(). */
        pix = fz_new_pixmap_from_page(ctx, page, fz_scale(s, s), fz_device_rgb(ctx), 0);
    }
    fz_always(ctx)
        fz_drop_page(ctx, page);
    fz_catch(ctx)
    {
        fz_report_error(ctx);
        pix = NULL;
    }
    return pix;
}

/* Open a document; on failure the previous one stays. */
static int open_document(const char *path)
{
    fz_document *newdoc = NULL;
    int count = 0;

    if (!ctx)
    {
        ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
        if (!ctx)
        {
            fprintf(stderr, "Folio: cannot create MuPDF context\n");
            return 0;
        }
        fz_register_document_handlers(ctx);
    }
    fz_var(newdoc);
    fz_try(ctx)
    {
        newdoc = fz_open_document(ctx, path);
        count = fz_count_pages(ctx, newdoc);
        if (count < 1)
            fz_throw(ctx, FZ_ERROR_FORMAT, "document has no pages");
    }
    fz_catch(ctx)
    {
        fz_drop_document(ctx, newdoc);
        fprintf(stderr, "Folio: cannot open %s: %s\n", path, fz_caught_message(ctx));
        if (win_obj)
            MUI_Request(app_obj, win_obj, 0, (CONST_STRPTR)"Folio", (CONST_STRPTR)"*_OK",
                        (CONST_STRPTR)"Cannot open\n%s\n\n%s", path, fz_caught_message(ctx));
        return 0;
    }
    fz_drop_document(ctx, doc);
    doc = newdoc;
    page_count = count;
    return 1;
}

/* --- Column geometry ------------------------------------------------------- */

static LONG attr(Object *obj, ULONG id)
{
    IPTR v = 0;
    GetAttr(id, obj, &v);
    return (LONG)v;
}

/* Distance from the top of the column to the top of a cell. */
static LONG cell_y(int kind, int page)
{
    LONG y = 0;
    int i;
    for (i = 0; i < page; i++)
        y += _height(cols[kind].cells[i]) + cols[kind].spacing;
    return y;
}

static void scroll_to(int kind, LONG y)
{
    SET(cols[kind].group, MUIA_Virtgroup_Top, y);
    /* The group clamps the offset; remember where it really ended up. */
    if (kind == KIND_MAIN)
        goto_top = attr(cols[kind].group, MUIA_Virtgroup_Top);
}

/* Relative scrolling. Unlike scroll_to() this is the user moving, so the
 * current page must follow the offset again. */
static void scroll_by(int kind, LONG delta)
{
    LONG top = attr(cols[kind].group, MUIA_Virtgroup_Top) + delta;
    SET(cols[kind].group, MUIA_Virtgroup_Top, top < 0 ? 0 : top);
    if (kind == KIND_MAIN)
        goto_top = -1;
}

static void set_current(int page)
{
    int old = current_page;
    struct Column *t = &cols[KIND_THUMB];

    if (page == old)
        return;
    current_page = page;
    status_text[0] = 0;
    update_label();

    /* Only the two thumbnails whose outline changed. */
    MUI_Redraw(t->cells[old], MADF_DRAWUPDATE);
    MUI_Redraw(t->cells[page], MADF_DRAWUPDATE);

    if (layout_valid)
    {
        /* Keep the current thumbnail in view. */
        LONG top = attr(t->group, MUIA_Virtgroup_Top);
        LONG vis = _mheight(t->group);
        LONG y = cell_y(KIND_THUMB, page), h = _height(t->cells[page]);
        if (y < top)
            scroll_to(KIND_THUMB, y);
        else if (y + h > top + vis)
            scroll_to(KIND_THUMB, y + h - vis);
    }
}

/* --- Cell: one page in a column --------------------------------------------- */

static IPTR Cell_New(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct CellData *d;
    fz_rect box = { 0, 0, 1, 1 };
    fz_page *page = NULL;
    float pw, ph;

    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg);
    if (!obj)
        return 0;
    d = INST_DATA(cl, obj);
    d->page = (LONG)GetTagData(MUIA_Cell_Page, 0, msg->ops_AttrList);
    d->kind = (LONG)GetTagData(MUIA_Cell_Kind, KIND_THUMB, msg->ops_AttrList);

    fz_var(page);
    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, d->page);
        box = fz_bound_page(ctx, page);
    }
    fz_always(ctx)
        fz_drop_page(ctx, page);
    fz_catch(ctx)
        fz_report_error(ctx);

    pw = box.x1 - box.x0; ph = box.y1 - box.y0;
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;
    d->box = box;
    d->aspect = ph / pw;
    /* Keep one absurdly tall or wide page from wrecking the column. */
    if (d->aspect > 3.0f) d->aspect = 3.0f;
    if (d->aspect < 0.2f) d->aspect = 0.2f;
    return (IPTR)obj;
}

static LONG cell_label_h(Object *obj, struct Column *c)
{
    return c->label ? _font(obj)->tf_YSize + 2 : 0;
}

static IPTR Cell_AskMinMax(struct IClass *cl, Object *obj, struct MUIP_AskMinMax *msg)
{
    struct CellData *d = INST_DATA(cl, obj);
    struct Column *c = &cols[d->kind];
    IPTR ret = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG h = (LONG)(c->w * d->aspect) + 2 * c->pad + cell_label_h(obj, c);

    if (d->kind == KIND_MAIN && zoom > 1.0f)
    {
        /* Wider than the view: the virtual group grows and scrolls. */
        msg->MinMaxInfo->MinWidth += c->w + 2 * c->pad;
        msg->MinMaxInfo->DefWidth += c->w + 2 * c->pad;
    }
    else
    {
        msg->MinMaxInfo->MinWidth  += CELL_W_MIN + 2 * c->pad;
        msg->MinMaxInfo->DefWidth  += c->w + 2 * c->pad;
    }
    msg->MinMaxInfo->MaxWidth   = MUI_MAXMAX;
    msg->MinMaxInfo->MinHeight += h; msg->MinMaxInfo->DefHeight += h; msg->MinMaxInfo->MaxHeight += h;
    return ret;
}

/* Where the page image sits inside a cell, in window coordinates, and how
 * many pixels one PDF unit is. Valid once the cell has been rendered. */
static int cell_image(Object *obj, struct CellData *d, LONG *x, LONG *y, LONG *tw, LONG *th, float *scale)
{
    struct Column *c = &cols[d->kind];
    LONG bw = _mwidth(obj) - 2 * c->pad;

    if (!d->pix)
        return 0;
    *tw = fz_pixmap_width(ctx, d->pix);
    *th = fz_pixmap_height(ctx, d->pix);
    if (*tw > bw) *tw = bw;
    *x = _mleft(obj) + (_mwidth(obj) - *tw) / 2;
    *y = _mtop(obj) + c->pad;
    *scale = (float)fz_pixmap_width(ctx, d->pix) / (d->box.x1 - d->box.x0);
    return *scale > 0;
}

/* Structured text of a page, or NULL with the error reported. */
static fz_stext_page *stext_of(int number)
{
    fz_stext_page *text = NULL;
    fz_var(text);
    fz_try(ctx)
    {
        fz_page *page = fz_load_page(ctx, doc, number);
        fz_try(ctx)
            text = fz_new_stext_page_from_page(ctx, page, NULL);
        fz_always(ctx)
            fz_drop_page(ctx, page);
        fz_catch(ctx)
            fz_rethrow(ctx);
    }
    fz_catch(ctx)
        fz_report_error(ctx);
    return text;
}

/* Cached structured text of a page; extracted on first use. */
static fz_stext_page *stext_cached(int number)
{
    int i, victim = 0;
    for (i = 0; i < MAX_STEXT; i++)
    {
        if (stext_cache[i].text && stext_cache[i].page == number)
        {
            stext_cache[i].used = ++stext_clock;
            return stext_cache[i].text;
        }
        if (stext_cache[i].used < stext_cache[victim].used)
            victim = i;
    }
    fz_drop_stext_page(ctx, stext_cache[victim].text);
    stext_cache[victim].text = stext_of(number);
    stext_cache[victim].page = number;
    stext_cache[victim].used = ++stext_clock;
    return stext_cache[victim].text;
}

static void drop_stext_cache(void)
{
    int i;
    for (i = 0; i < MAX_STEXT; i++)
    {
        fz_drop_stext_page(ctx, stext_cache[i].text);
        stext_cache[i].text = NULL;
    }
}

static int sel_first(void) { return sel_page < sel_end_page ? sel_page : sel_end_page; }
static int sel_last(void)  { return sel_page < sel_end_page ? sel_end_page : sel_page; }

/* The two points bounding the selection on one page. On a middle page that
 * is the whole page; on the first or last it runs to the page corner. Points
 * are ordered so MuPDF sees them in reading order. */
static void sel_span(int page, const fz_rect *box, fz_point *a, fz_point *b)
{
    fz_point start = sel_a, end = sel_b;
    int sp = sel_page, ep = sel_end_page;
    if (sp > ep || (sp == ep && (start.y > end.y || (start.y == end.y && start.x > end.x))))
    {
        fz_point t = start; start = end; end = t;
        int tp = sp; sp = ep; ep = tp;
    }
    *a = page == sp ? start : fz_make_point(box->x0, box->y0);
    *b = page == ep ? end   : fz_make_point(box->x1, box->y1);
}

/* Take the selection's inversion off the screen without a redraw: a second
 * inversion of the same quads restores the page. Falls back to a redraw
 * when the cell cannot be drawn into. */
static void invert_selection(Object *obj, struct CellData *d, struct RastPort *rp, LONG ox, LONG oy,
                             int sp, int ep, fz_point pa, fz_point pb);

static void redraw_selection_pages(void)
{
    int i;
    if (sel_page < 0)
        return;
    for (i = sel_first(); i <= sel_last(); i++)
    {
        Object *cell = cols[KIND_MAIN].cells[i];
        struct RastPort *rp = layout_valid ? _rp(cell) : NULL;
        if (rp)
            invert_selection(cell, INST_DATA(CellClass->mcc_Class, cell), rp, 0, 0,
                             sel_page, sel_end_page, sel_a, sel_b);
        else
            MUI_Redraw(cell, MADF_DRAWOBJECT);
    }
}

static void drop_selection(void)
{
    sel_page = sel_end_page = -1;
    selecting = FALSE;
}

/* Invert the selected quads over the freshly drawn page. */
static void invert_selection(Object *obj, struct CellData *d, struct RastPort *rp, LONG ox, LONG oy,
                             int sp, int ep, fz_point pa, fz_point pb);

static void draw_selection_at(Object *obj, struct CellData *d, struct RastPort *rp, LONG ox, LONG oy)
{
    LONG x, y, tw, th;
    float scale;
    int i;

    if (d->kind != KIND_MAIN)
        return;
    if (!cell_image(obj, d, &x, &y, &tw, &th, &scale))
        return;
    x += ox; y += oy;

    /* Search hits: a frame in the fill colour around each. */
    if (d->page == search_page)
    {
        SetAPen(rp, _dri(obj)->dri_Pens[FILLPEN]);
        for (i = 0; i < search_n; i++)
        {
            fz_rect r = fz_rect_from_quad(search_hits[i]);
            LONG x0 = x + (LONG)((r.x0 - d->box.x0) * scale) - 2;
            LONG y0 = y + (LONG)((r.y0 - d->box.y0) * scale) - 2;
            LONG x1 = x + (LONG)((r.x1 - d->box.x0) * scale) + 2;
            LONG y1 = y + (LONG)((r.y1 - d->box.y0) * scale) + 2;
            Move(rp, x0, y0); Draw(rp, x1, y0);
            Draw(rp, x1, y1); Draw(rp, x0, y1); Draw(rp, x0, y0);
        }
    }

    if (sel_page < 0 || d->page < sel_first() || d->page > sel_last())
        return;
    invert_selection(obj, d, rp, ox, oy, sel_page, sel_end_page, sel_a, sel_b);
}

/* Invert the quads of a selection (given explicitly, so a previous state
 * can be undone on screen) on one page. Inversion is its own inverse: the
 * XOR of old and new state repaints only what changed. */
static void invert_selection(Object *obj, struct CellData *d, struct RastPort *rp, LONG ox, LONG oy,
                             int sp, int ep, fz_point pa, fz_point pb)
{
    fz_quad quads[512];
    fz_stext_page *text;
    fz_point a, b, start = pa, end = pb;
    LONG x, y, tw, th;
    float scale;
    int i, n, lo = sp < ep ? sp : ep, hi = sp < ep ? ep : sp;

    if (sp < 0 || d->page < lo || d->page > hi)
        return;
    if (!cell_image(obj, d, &x, &y, &tw, &th, &scale))
        return;
    x += ox; y += oy;
    text = stext_cached(d->page);
    if (!text)
        return;
    if (sp > ep || (sp == ep && (start.y > end.y || (start.y == end.y && start.x > end.x))))
    {
        fz_point t = start; start = end; end = t;
        int tp = sp; sp = ep; ep = tp;
    }
    a = d->page == sp ? start : fz_make_point(d->box.x0, d->box.y0);
    b = d->page == ep ? end   : fz_make_point(d->box.x1, d->box.y1);
    n = fz_highlight_selection(ctx, text, a, b, quads, 512);
    SetDrMd(rp, COMPLEMENT);
    for (i = 0; i < n; i++)
    {
        fz_rect r = fz_rect_from_quad(quads[i]);
        LONG x0 = x + (LONG)((r.x0 - d->box.x0) * scale);
        LONG y0 = y + (LONG)((r.y0 - d->box.y0) * scale);
        LONG x1 = x + (LONG)((r.x1 - d->box.x0) * scale);
        LONG y1 = y + (LONG)((r.y1 - d->box.y0) * scale);
        if (x0 < x) x0 = x;
        if (y0 < y) y0 = y;
        if (x1 > x + tw - 1) x1 = x + tw - 1;
        if (y1 > y + th - 1) y1 = y + th - 1;
        if (x1 >= x0 && y1 >= y0)
            RectFill(rp, x0, y0, x1, y1);
    }
    SetDrMd(rp, JAM1);
}

/* Drop the rendered cell of this column that was drawn longest ago. */
static void evict_oldest(struct Column *c, struct IClass *cl, Object *keep)
{
    struct CellData *oldest = NULL;
    int i;

    for (i = 0; i < page_count; i++)
    {
        struct CellData *e = INST_DATA(cl, c->cells[i]);
        if (e->pix && c->cells[i] != keep && (!oldest || e->stamp < oldest->stamp))
            oldest = e;
    }
    if (oldest)
    {
        fz_drop_pixmap(ctx, oldest->pix);
        oldest->pix = NULL;
        c->live--;
    }
}

static void cell_paint(Object *obj, struct CellData *d, struct RastPort *rp, LONG ox, LONG oy, LONG bw, LONG bh);

static IPTR Cell_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg)
{
    struct CellData *d = INST_DATA(cl, obj);
    struct Column *c = &cols[d->kind];
    struct RastPort *rp;
    LONG l, t, w, h, bw, bh;

    DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
        return 0;

    rp = _rp(obj);
    l = _mleft(obj); t = _mtop(obj); w = _mwidth(obj); h = _mheight(obj);

    /* The page goes into the box this object really got; in the page
     * column that box is the view width times the zoom. */
    if (d->kind == KIND_MAIN)
    {
        LONG vw = _mwidth(c->group);
        if (vw > 0) view_w = vw;
        bw = (LONG)((view_w - 2 * c->pad) * zoom);
        if (zoom <= 1.0f && bw > w - 2 * c->pad) bw = w - 2 * c->pad;
    }
    else
        bw = w - 2 * c->pad;
    bh = h - 2 * c->pad - cell_label_h(obj, c);
    if (bw < 8) bw = 8;
    if (bh < 8) bh = 8;

    if (!c->pending && (bw > c->w + CELL_SLACK || bw < c->w - CELL_SLACK))
    {
        /* Heights were computed for another width. Layout cannot change
         * inside a draw, so queue it on the application. */
        c->pending = TRUE;
        DoMethod(app_obj, MUIM_Application_PushMethod, (IPTR)reader_obj, 3,
                 MUIM_Reader_Relayout, d->kind, bw);
    }

    /* Zune's scrollgroup moves its contents with MUIA_NoNotify (muimaster
     * scrollgroup.c), so there is no scroll notification to listen to. Every
     * scroll does redraw page cells, though; let that queue one check. */
    if (d->kind == KIND_MAIN && !scroll_check_pending)
    {
        scroll_check_pending = TRUE;
        DoMethod(app_obj, MUIM_Application_PushMethod, (IPTR)reader_obj, 1,
                 MUIM_Reader_Scrolled);
    }

    /* Lazy: a virtual group only draws what is visible, so a long document
     * pays for a page when it scrolls into view. */
    if (!d->pix || d->boxW != bw || d->boxH != bh)
    {
        if (d->pix)
        {
            fz_drop_pixmap(ctx, d->pix);
            c->live--;
        }
        {
            LONG t0 = now_ms();
            d->pix = render_fit(d->page, bw, bh);
            if (d->kind == KIND_MAIN && show_timing)
            {
                last_ms = now_ms() - t0;
                if (last_ms > worst_ms) worst_ms = last_ms;
                /* A text change redraws the label, not this column. */
                DoMethod(app_obj, MUIM_Application_PushMethod, (IPTR)reader_obj, 1,
                         MUIM_Reader_ShowTiming);
            }
        }
        d->boxW = bw; d->boxH = bh;
        if (d->pix && ++c->live > c->limit)
            evict_oldest(c, cl, obj);
    }
    d->stamp = ++draw_clock;

    /* Compose off screen and blit once, so the eye never sees background,
     * page and selection arrive as separate steps. The buffer keeps window
     * coordinates (a scrolled layer origin), so the helpers that map page
     * space to the window need no second version. */
    {
        struct RastPort *win_rp = rp;
        struct BitMap *bm = AllocBitMap(w, h, GetBitMapAttr(win_rp->BitMap, BMA_DEPTH),
                                        BMF_MINPLANES, win_rp->BitMap);
        struct RastPort buf;
        if (bm)
        {
            InitRastPort(&buf);
            buf.BitMap = bm;
            buf.Layer = NULL;
            /* Mirror the background into the buffer, then draw there. */
            DoMethod(obj, MUIM_DrawBackground, l, t, w, h, l, t, 0);
            ClipBlit(win_rp, l, t, &buf, 0, 0, w, h, 0xC0);
            SetFont(&buf, _font(obj));
            cell_paint(obj, d, &buf, -l, -t, bw, bh);
            BltBitMapRastPort(bm, 0, 0, win_rp, l, t, w, h, 0xC0);
            FreeBitMap(bm);
            return 0;
        }
    }
    DoMethod(obj, MUIM_DrawBackground, l, t, w, h, l, t, 0);
    cell_paint(obj, d, rp, 0, 0, bw, bh);
    return 0;
}

/* Page image, selection, search frames, outline and label, into rp with
 * the window origin shifted by (ox, oy). */
static void cell_paint(Object *obj, struct CellData *d, struct RastPort *rp, LONG ox, LONG oy, LONG bw, LONG bh)
{
    struct Column *c = &cols[d->kind];
    LONG l = _mleft(obj), t = _mtop(obj), w = _mwidth(obj);
    LONG x, y, tw, th;

    l += ox; t += oy;
    tw = d->pix ? fz_pixmap_width(ctx, d->pix) : bw;
    th = d->pix ? fz_pixmap_height(ctx, d->pix) : bh;
    if (tw > bw) tw = bw;
    if (th > bh) th = bh;
    x = l + (w - tw) / 2;
    y = t + c->pad;
    if (d->pix)
        WritePixelArray(fz_pixmap_samples(ctx, d->pix), 0, 0,
                        fz_pixmap_stride(ctx, d->pix), rp, x, y, tw, th, RECTFMT_RGB);
    draw_selection_at(obj, d, rp, ox, oy);

    /* Outline: thin shadow for every page, thick fill colour for the current
     * thumbnail. */
    if (d->kind == KIND_THUMB && d->page == current_page)
    {
        SetAPen(rp, _dri(obj)->dri_Pens[FILLPEN]);
        RectFill(rp, x - 3, y - 3, x + tw + 2, y - 1);
        RectFill(rp, x - 3, y + th, x + tw + 2, y + th + 2);
        RectFill(rp, x - 3, y - 3, x - 1, y + th + 2);
        RectFill(rp, x + tw, y - 3, x + tw + 2, y + th + 2);
    }
    else
    {
        SetAPen(rp, _dri(obj)->dri_Pens[SHADOWPEN]);
        Move(rp, x - 1, y - 1);
        Draw(rp, x + tw, y - 1);
        Draw(rp, x + tw, y + th);
        Draw(rp, x - 1, y + th);
        Draw(rp, x - 1, y - 1);
    }

    if (c->label)
    {
        char num[16];
        LONG len = snprintf(num, sizeof(num), "%ld", (long)d->page + 1);
        SetFont(rp, _font(obj));
        SetAPen(rp, _dri(obj)->dri_Pens[TEXTPEN]);
        SetDrMd(rp, JAM1);
        Move(rp, l + (w - TextLength(rp, (CONST_STRPTR)num, len)) / 2,
                 y + th + 4 + _font(obj)->tf_Baseline);
        Text(rp, (CONST_STRPTR)num, len);
    }
}

static void go_to(int target);
static void copy_selection(void);
static void copy_image(void);
static void highlight_selection(void);
static int save_document(const char *path);
static void save_as(void);
static char doc_path[512];
static BOOL modified;

static IPTR Cell_ContextMenuChoice(struct IClass *cl, Object *obj, struct MUIP_ContextMenuChoice *msg)
{
    IPTR id = 0;
    GetAttr(MUIA_UserData, msg->item, &id);
    if (id == MEN_COPY)      copy_selection();
    if (id == MEN_COPYIMAGE) copy_image();
    if (id == MEN_HIGHLIGHT) highlight_selection();
    return 0;
}

/* Keyboard and wheel belong to the window, not to a page, but MUI delivers
 * events through an area object. The first page cell hosts the handler. */
static IPTR Cell_Setup(struct IClass *cl, Object *obj, Msg msg)
{
    struct CellData *d = INST_DATA(cl, obj);

    if (!DoSuperMethodA(cl, obj, msg))
        return FALSE;
    if (d->kind == KIND_MAIN && d->page == 0)
    {
        d->ehn.ehn_Object = obj;
        d->ehn.ehn_Class = cl;
        d->ehn.ehn_Events = IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS;
        d->ehn.ehn_Priority = 0;
        d->ehn.ehn_Flags = 0;
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
        d->has_handler = TRUE;
    }
    return TRUE;
}

static IPTR Cell_Cleanup(struct IClass *cl, Object *obj, Msg msg)
{
    struct CellData *d = INST_DATA(cl, obj);

    if (d->has_handler)
    {
        DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
        d->has_handler = FALSE;
    }
    return DoSuperMethodA(cl, obj, msg);
}

/* Mouse motion is only wanted while a selection is being dragged. */
static void want_mousemove(struct CellData *host, Object *obj, BOOL on)
{
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&host->ehn);
    if (on) host->ehn.ehn_Events |= IDCMP_MOUSEMOVE;
    else    host->ehn.ehn_Events &= ~IDCMP_MOUSEMOVE;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&host->ehn);
}

/* Window position to a point on a page. Returns the page cell, or NULL when
 * the position is not over the visible part of a rendered page. */
static Object *page_at(struct IClass *cl, LONG mx, LONG my, int only_page, fz_point *pt)
{
    struct Column *m = &cols[KIND_MAIN];
    int i;

    if (only_page < 0 && (mx < _mleft(m->group) || mx > _mright(m->group) ||
                          my < _mtop(m->group) || my > _mbottom(m->group)))
        return NULL;
    for (i = 0; i < page_count; i++)
    {
        Object *cell = m->cells[i];
        struct CellData *e = INST_DATA(cl, cell);
        LONG x, y, tw, th;
        float scale;

        if (only_page >= 0 ? i != only_page : (my < _top(cell) || my > _bottom(cell)))
            continue;
        if (!cell_image(cell, e, &x, &y, &tw, &th, &scale))
            return NULL;
        /* While dragging, a pointer outside the page still moves the end
         * point: clamp it to the page. */
        if (mx < x) mx = x;
        if (mx > x + tw - 1) mx = x + tw - 1;
        if (my < y) my = y;
        if (my > y + th - 1) my = y + th - 1;
        pt->x = e->box.x0 + (mx - x) / scale;
        pt->y = e->box.y0 + (my - y) / scale;
        return cell;
    }
    return NULL;
}

#define MUIM_Reader_Autoscroll 0x80440007UL

static void set_autoscroll(BOOL on)
{
    if (on == autoscroll_on)
        return;
    if (on)
    {
        autoscroll_ihn.ihn_Object = reader_obj;
        autoscroll_ihn.ihn_Flags = MUIIHNF_TIMER;
        autoscroll_ihn.ihn_Millis = AUTOSCROLL_MS;
        autoscroll_ihn.ihn_Method = MUIM_Reader_Autoscroll;
        DoMethod(app_obj, MUIM_Application_AddInputHandler, (IPTR)&autoscroll_ihn);
    }
    else
        DoMethod(app_obj, MUIM_Application_RemInputHandler, (IPTR)&autoscroll_ihn);
    autoscroll_on = on;
}

static IPTR handle_mouse(struct IClass *cl, Object *obj, struct IntuiMessage *imsg)
{
    struct CellData *host = INST_DATA(cl, obj);
    fz_point pt;
    Object *cell;

    if (imsg->Class == IDCMP_MOUSEBUTTONS && imsg->Code == SELECTDOWN)
    {
        cell = page_at(cl, imsg->MouseX, imsg->MouseY, -1, &pt);
        redraw_selection_pages();
        drop_selection();
        if (!cell)
            return 0;
        sel_page = sel_end_page = ((struct CellData *)INST_DATA(cl, cell))->page;
        sel_a = sel_b = pt;
        selecting = TRUE;
        want_mousemove(host, obj, TRUE);
        /* Not eaten: a plain click must still reach whatever is under it. */
        return 0;
    }
    if (!selecting)
        return 0;
    if (imsg->Class == IDCMP_MOUSEMOVE ||
        (imsg->Class == IDCMP_MOUSEBUTTONS && imsg->Code == SELECTUP))
    {
        struct Column *m = &cols[KIND_MAIN];
        LONG my = imsg->MouseY;
        int old_end = sel_end_page, i;

        /* Outside the view: keep the end on the edge page and let the
         * timer scroll until the pointer comes back. */
        autoscroll_dy = 0;
        if (my < _mtop(m->group))    { autoscroll_dy = (my - _mtop(m->group)) / 2 - 4;    my = _mtop(m->group); }
        if (my > _mbottom(m->group)) { autoscroll_dy = (my - _mbottom(m->group)) / 2 + 4; my = _mbottom(m->group); }
        set_autoscroll(autoscroll_dy != 0 && imsg->Class == IDCMP_MOUSEMOVE);

        cell = page_at(cl, imsg->MouseX, my, -1, &pt);
        if (!cell)
        {
            /* In the gap between two pages: snap to the nearer one. */
            for (i = 0; i < page_count; i++)
                if (my < _top(m->cells[i])) break;
            if (i > 0 && (i == page_count || my - _bottom(m->cells[i - 1]) < _top(m->cells[i]) - my))
                i--;
            if (i < page_count)
                cell = page_at(cl, imsg->MouseX, my, i, &pt);
        }
        if (cell)
        {
            /* Undo the old inversion and apply the new one directly on
             * screen: the page image is not touched, so nothing flickers. */
            {
                fz_point old_b = sel_b;
                int lo, hi;
                sel_end_page = ((struct CellData *)INST_DATA(cl, cell))->page;
                sel_b = pt;
                lo = old_end < sel_end_page ? old_end : sel_end_page;
                hi = old_end < sel_end_page ? sel_end_page : old_end;
                if (sel_page < lo) lo = sel_page;
                if (sel_page > hi) hi = sel_page;
                for (i = lo; i <= hi; i++)
                {
                    struct CellData *e = INST_DATA(cl, m->cells[i]);
                    struct RastPort *rp = _rp(m->cells[i]);
                    if (!rp)
                        continue;
                    invert_selection(m->cells[i], e, rp, 0, 0, sel_page, old_end, sel_a, old_b);
                    invert_selection(m->cells[i], e, rp, 0, 0, sel_page, sel_end_page, sel_a, sel_b);
                }
            }
        }
        if (imsg->Class == IDCMP_MOUSEBUTTONS)
        {
            selecting = FALSE;
            set_autoscroll(FALSE);
            want_mousemove(host, obj, FALSE);
        }
    }
    return 0;
}

static IPTR Cell_HandleEvent(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *msg)
{
    struct Column *m = &cols[KIND_MAIN], *t = &cols[KIND_THUMB];
    LONG screenful;
    int wheel_kind;

    if (!msg->imsg || !layout_valid)
        return 0;
    if (msg->imsg->Class == IDCMP_MOUSEBUTTONS || msg->imsg->Class == IDCMP_MOUSEMOVE)
        return handle_mouse(cl, obj, msg->imsg);
    if (msg->imsg->Class != IDCMP_RAWKEY)
        return 0;
    if (msg->imsg->Code & IECODE_UP_PREFIX)
        return 0;

    /* While the pointer is over a page cell Zune sets WFLG_RMBTRAP for the
     * context menu, and Intuition then ignores menu shortcuts. So the edit
     * shortcuts are handled here as well; the menu items stay for the mouse. */
    if (msg->imsg->Qualifier & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))
    {
        switch (msg->imsg->Code)
        {
            case RAWKEY_C: copy_selection(); return MUI_EventHandlerRC_Eat;
            case RAWKEY_I: copy_image();     return MUI_EventHandlerRC_Eat;
            case RAWKEY_H: highlight_selection(); return MUI_EventHandlerRC_Eat;
            case RAWKEY_EQUAL: case RAWKEY_KP_PLUS:  DoMethod(reader_obj, MUIM_Reader_Zoom, ZOOM_IN);  return MUI_EventHandlerRC_Eat;
            case RAWKEY_MINUS: case RAWKEY_KP_MINUS: DoMethod(reader_obj, MUIM_Reader_Zoom, ZOOM_OUT); return MUI_EventHandlerRC_Eat;
            case RAWKEY_0: DoMethod(reader_obj, MUIM_Reader_Zoom, ZOOM_FITWIDTH); return MUI_EventHandlerRC_Eat;
            case RAWKEY_9: DoMethod(reader_obj, MUIM_Reader_Zoom, ZOOM_FITPAGE); return MUI_EventHandlerRC_Eat;
            case RAWKEY_S: if (modified) save_document(doc_path); return MUI_EventHandlerRC_Eat;
            case RAWKEY_A: save_as(); return MUI_EventHandlerRC_Eat;
            default: return 0;
        }
    }

    screenful = _mheight(m->group) - LINE_STEP;
    if (screenful < LINE_STEP) screenful = LINE_STEP;
    /* The wheel scrolls whichever column the pointer is over. */
    wheel_kind = (msg->imsg->MouseX >= _left(t->group) && msg->imsg->MouseX <= _right(t->group))
               ? KIND_THUMB : KIND_MAIN;

    switch (msg->imsg->Code)
    {
        case RAWKEY_UP:            scroll_by(KIND_MAIN, -LINE_STEP); break;
        case RAWKEY_DOWN:          scroll_by(KIND_MAIN, LINE_STEP); break;
        case RAWKEY_PAGEUP:        scroll_by(KIND_MAIN, -screenful); break;
        case RAWKEY_PAGEDOWN:
        case RAWKEY_SPACE:         scroll_by(KIND_MAIN, screenful); break;
        case RAWKEY_HOME:          go_to(0); break;
        case RAWKEY_END:           go_to(page_count - 1); break;
        case RAWKEY_LEFT:          go_to(current_page - 1); break;
        case RAWKEY_RIGHT:         go_to(current_page + 1); break;
        case RAWKEY_NM_WHEEL_UP:   scroll_by(wheel_kind, -WHEEL_STEP); break;
        case RAWKEY_NM_WHEEL_DOWN: scroll_by(wheel_kind, WHEEL_STEP); break;
        default:                   return 0;
    }
    return MUI_EventHandlerRC_Eat;
}

static IPTR Cell_Dispose(struct IClass *cl, Object *obj, Msg msg)
{
    struct CellData *d = INST_DATA(cl, obj);
    fz_drop_pixmap(ctx, d->pix);
    d->pix = NULL;
    return DoSuperMethodA(cl, obj, msg);
}

BOOPSI_DISPATCHER(IPTR, CellDispatcher, cl, obj, msg)
{
    switch (msg->MethodID) {
        case OM_NEW:         return Cell_New(cl, obj, (struct opSet *)msg);
        case MUIM_AskMinMax: return Cell_AskMinMax(cl, obj, (struct MUIP_AskMinMax *)msg);
        case MUIM_Draw:      return Cell_Draw(cl, obj, (struct MUIP_Draw *)msg);
        case MUIM_Setup:     return Cell_Setup(cl, obj, msg);
        case MUIM_Cleanup:   return Cell_Cleanup(cl, obj, msg);
        case MUIM_HandleEvent: return Cell_HandleEvent(cl, obj, (struct MUIP_HandleEvent *)msg);
        case MUIM_ContextMenuChoice: return Cell_ContextMenuChoice(cl, obj, (struct MUIP_ContextMenuChoice *)msg);
        case OM_DISPOSE:     return Cell_Dispose(cl, obj, msg);
        default:             return DoSuperMethodA(cl, obj, msg);
    }
}
BOOPSI_DISPATCHER_END

/* --- Reader: receives the application's private methods -------------------- */

static void go_to(int target)
{
    if (target < 0) target = 0;
    if (target > page_count - 1) target = page_count - 1;
    if (layout_valid)
        scroll_to(KIND_MAIN, cell_y(KIND_MAIN, target));
    set_current(target);
}

static IPTR Reader_Relayout(struct MUIP_Reader_Relayout *msg)
{
    struct Column *c = &cols[msg->kind];

    c->w = msg->width < CELL_W_MIN ? CELL_W_MIN : msg->width;
    /* An empty change bracket makes the group ask its children for their
     * sizes again; they answer with heights for the new width. For the
     * page column the bracket goes around the scrollgroup: Zune's
     * ExitChange re-lays out only the bracketed group (muimaster group.c,
     * RecalcDisplay), and it is the scrollgroup's layout hook that decides
     * whether a horizontal scroller is needed. */
    {
        Object *target = (msg->kind == KIND_MAIN && pages_scroll) ? pages_scroll : c->group;
        if (DoMethod(target, MUIM_Group_InitChange))
            DoMethod(target, MUIM_Group_ExitChange);
    }
    c->pending = FALSE;
    /* Every height changed, so the old offset now points somewhere else. */
    if (layout_valid)
        scroll_to(msg->kind, cell_y(msg->kind, current_page));
    return 0;
}

static IPTR Reader_Zoom(struct MUIP_Reader_Zoom *msg)
{
    struct Column *m = &cols[KIND_MAIN];
    float z = zoom;
    switch (msg->mode)
    {
        case ZOOM_IN:  z *= ZOOM_STEP; break;
        case ZOOM_OUT: z /= ZOOM_STEP; break;
        case ZOOM_FITWIDTH: z = 1.0f; break;
        case ZOOM_FITPAGE:
        {
            /* Scale so the current page's height fits the view. */
            struct CellData *d = INST_DATA(CellClass->mcc_Class, m->cells[current_page]);
            LONG vh = _mheight(m->group) - 2 * m->spacing, vw = view_w > 0 ? view_w : _mwidth(m->group);
            if (vh > 0 && vw > 0 && d->aspect > 0)
                z = ((float)vh / d->aspect) / (vw - 2 * m->pad);
            if (z > 1.0f) z = 1.0f;
            break;
        }
    }
    if (z < ZOOM_MIN) z = ZOOM_MIN;
    if (z > ZOOM_MAX) z = ZOOM_MAX;
    if (z == zoom)
        return 0;
    zoom = z;
    snprintf(status_text, sizeof(status_text), "zoom %d%%", (int)(zoom * 100 + 0.5f));
    update_label();
    /* Every cell's width and height change; the draw that follows finds
     * the new size and re-renders lazily. */
    m->pending = TRUE;
    DoMethod(app_obj, MUIM_Application_PushMethod, (IPTR)reader_obj, 3,
             MUIM_Reader_Relayout, KIND_MAIN, (LONG)((view_w - 2 * m->pad) * zoom));
    return 0;
}

static IPTR Reader_Scrolled(void)
{
    struct Column *m = &cols[KIND_MAIN];
    LONG top, probe, y = 0;
    int i;

    scroll_check_pending = FALSE;
    if (!layout_valid)
        return 0;
    top = attr(m->group, MUIA_Virtgroup_Top);
    /* Still where an explicit jump left it: keep the page that was asked
     * for. Near the end the column cannot scroll far enough for the offset
     * alone to identify it. */
    if (top == goto_top)
        return 0;
    goto_top = -1;
    /* The current page is the one a third of the way down the view. */
    probe = top + _mheight(m->group) / 3;
    for (i = 0; i < page_count - 1; i++)
    {
        y += _height(m->cells[i]) + m->spacing;
        if (y > probe)
            break;
    }
    set_current(i);
    return 0;
}

static int fill_column(int kind, Object *group);
static void drop_stext_cache(void);
static void drop_selection(void);

static void clear_column(int kind)
{
    struct Column *c = &cols[kind];
    int i;
    if (!c->cells)
        return;
    if (DoMethod(c->group, MUIM_Group_InitChange))
    {
        for (i = 0; i < page_count; i++)
            if (c->cells[i])
            {
                DoMethod(c->group, OM_REMMEMBER, (IPTR)c->cells[i]);
                MUI_DisposeObject(c->cells[i]);
            }
        DoMethod(c->group, MUIM_Group_ExitChange);
    }
    free(c->cells);
    c->cells = NULL;
    c->live = 0;
}

/* Replace the shown document with the one at path. Everything that refers
 * to pages goes first, then the columns are rebuilt. */
/* Cells added to an open window's group only get set up and laid out
 * inside a change bracket. */
static int refill(int kind, Object *group)
{
    int ok, open = layout_valid && DoMethod(group, MUIM_Group_InitChange);
    ok = fill_column(kind, group);
    if (open)
        DoMethod(group, MUIM_Group_ExitChange);
    return ok;
}

static int confirm_discard(void);
static void set_title(void);

/* Flatten the outline into the list, one entry per node, indented by
 * depth. Only entries that resolve to a page are added. */
static void add_outline(fz_outline *node, int depth)
{
    char line[256];
    for (; node && outline_n < OUTLINE_MAX; node = node->next)
    {
        int page = fz_page_number_from_location(ctx, doc, node->page);
        if (page < 0 && node->uri)
            page = fz_page_number_from_location(ctx, doc, fz_resolve_link(ctx, doc, node->uri, NULL, NULL));
        if (page >= 0 && node->title)
        {
            int ind = depth * 2 > 20 ? 20 : depth * 2;
            snprintf(line, sizeof(line), "%*s%s", ind, "", node->title);
            outline_pages[outline_n++] = page;
            DoMethod(outline_list, MUIM_List_InsertSingle, (IPTR)line, MUIV_List_Insert_Bottom);
        }
        if (node->down)
            add_outline(node->down, depth + 1);
    }
}

static void fill_outline(void)
{
    fz_outline *root = NULL;

    outline_quiet = TRUE;
    DoMethod(outline_list, MUIM_List_Clear);
    outline_n = 0;
    fz_var(root);
    fz_try(ctx)
    {
        root = fz_load_outline(ctx, doc);
        add_outline(root, 0);
    }
    fz_always(ctx)
        fz_drop_outline(ctx, root);
    fz_catch(ctx)
        fz_report_error(ctx);
    if (outline_n == 0)
        DoMethod(outline_list, MUIM_List_InsertSingle, (IPTR)"(no outline)", MUIV_List_Insert_Bottom);
    outline_quiet = FALSE;
}

static IPTR Reader_OutlinePick(void)
{
    IPTR active = 0;
    if (outline_quiet)
        return 0;
    GetAttr(MUIA_List_Active, outline_list, &active);
    if ((LONG)active >= 0 && (LONG)active < outline_n)
        go_to(outline_pages[active]);
    return 0;
}

static int load_document(const char *path)
{
    Object *tg = cols[KIND_THUMB].group, *pg = cols[KIND_MAIN].group;
    int had = page_count;

    if (!confirm_discard())
        return 0;
    set_autoscroll(FALSE);
    drop_selection();
    drop_stext_cache();
    search_page = -1; search_n = 0;
    clear_column(KIND_THUMB);
    clear_column(KIND_MAIN);
    if (!open_document(path))
    {
        /* Rebuild what was there. */
        page_count = had;
        if (doc)
        {
            refill(KIND_THUMB, tg);
            refill(KIND_MAIN, pg);
        }
        return 0;
    }
    current_page = 0;
    goto_top = -1;
    status_text[0] = 0;
    zoom = 1.0f;
    if (outline_list)
        fill_outline();
    if (!refill(KIND_THUMB, tg) || !refill(KIND_MAIN, pg))
        return 0;
    strncpy(doc_path, path, sizeof(doc_path) - 1);
    doc_path[sizeof(doc_path) - 1] = 0;
    modified = FALSE;
    set_title();
    update_label();
    if (layout_valid)
    {
        scroll_to(KIND_MAIN, 0);
        scroll_to(KIND_THUMB, 0);
    }
    return 1;
}

static int ask_file(char *path, size_t size);

static IPTR Reader_Open(void)
{
    static char chosen[512];
    if (ask_file(chosen, sizeof(chosen)))
        load_document(chosen);
    return 0;
}

/* Workbench dropped icons on the window: open the first one. */
static IPTR Reader_Drop(struct AppMessage *am)
{
    char path[512];
    if (!am || am->am_NumArgs < 1 || !am->am_ArgList)
        return 0;
    if (!NameFromLock(am->am_ArgList[0].wa_Lock, (STRPTR)path, sizeof(path)))
        return 0;
    if (am->am_ArgList[0].wa_Name && am->am_ArgList[0].wa_Name[0])
        if (!AddPart((STRPTR)path, (CONST_STRPTR)am->am_ArgList[0].wa_Name, sizeof(path)))
            return 0;
    load_document(path);
    return 0;
}

/* Next page with a hit, starting after the page of the last hit (or at the
 * current page), wrapping around once. Extracts text page by page on the
 * UI task; a long document takes a moment. */
static IPTR Reader_Find(void)
{
    IPTR needle = 0;
    int start, k, old = search_page;

    GetAttr(MUIA_String_Contents, search_field, &needle);
    if (!needle || !((char *)needle)[0])
        return 0;
    start = search_page >= 0 ? search_page + 1 : current_page;
    for (k = 0; k < page_count; k++)
    {
        int n, number = (start + k) % page_count;
        fz_stext_page *text = stext_of(number);
        if (!text)
            continue;
        n = fz_search_stext_page(ctx, text, (const char *)needle, NULL, search_hits, MAX_HITS);
        fz_drop_stext_page(ctx, text);
        if (n > 0)
        {
            search_page = number;
            search_n = n;
            if (old >= 0 && old != number)
                MUI_Redraw(cols[KIND_MAIN].cells[old], MADF_DRAWUPDATE);
            go_to(number);
            MUI_Redraw(cols[KIND_MAIN].cells[number], MADF_DRAWUPDATE);
            return 0;
        }
    }
    /* Nothing anywhere: clear the old frame. */
    search_page = -1;
    search_n = 0;
    if (old >= 0)
        MUI_Redraw(cols[KIND_MAIN].cells[old], MADF_DRAWUPDATE);
    DisplayBeep(NULL);
    return 0;
}

BOOPSI_DISPATCHER(IPTR, ReaderDispatcher, cl, obj, msg)
{
    switch (msg->MethodID) {
        case MUIM_Reader_Step:     go_to(current_page + (int)((struct MUIP_Reader_Step *)msg)->delta); return 0;
        case MUIM_Reader_Goto:     go_to((int)((struct MUIP_Reader_Goto *)msg)->page); return 0;
        case MUIM_Reader_Relayout: return Reader_Relayout((struct MUIP_Reader_Relayout *)msg);
        case MUIM_Reader_Scrolled: return Reader_Scrolled();
        case MUIM_Reader_ShowTiming: update_label(); return 0;
        case MUIM_Reader_Find:     return Reader_Find();
        case MUIM_Reader_Drop:     return Reader_Drop((struct AppMessage *)((IPTR *)msg)[1]);
        case MUIM_Reader_Zoom:     return Reader_Zoom((struct MUIP_Reader_Zoom *)msg);
        case MUIM_Reader_OutlinePick: return Reader_OutlinePick();
        case MUIM_Reader_Autoscroll: if (selecting) scroll_by(KIND_MAIN, autoscroll_dy); return 0;
        default:                   return DoSuperMethodA(cl, obj, msg);
    }
}
BOOPSI_DISPATCHER_END

/* --- Clipboard ---------------------------------------------------------------- */

/* UTF-8 to the 8-bit text every AROS program can paste. Code points up to
 * U+00FF map to Latin-1; common typographic characters get ASCII stand-ins;
 * the rest become '?'. The UTF8 chunk written next to it loses nothing. */
static size_t utf8_to_latin1(const char *in, char *out)
{
    const unsigned char *s = (const unsigned char *)in;
    size_t n = 0;

    while (*s)
    {
        unsigned long cp;
        int extra;

        if (*s < 0x80)      { cp = *s; extra = 0; }
        else if (*s < 0xE0) { cp = *s & 0x1F; extra = 1; }
        else if (*s < 0xF0) { cp = *s & 0x0F; extra = 2; }
        else                { cp = *s & 0x07; extra = 3; }
        s++;
        while (extra-- > 0 && (*s & 0xC0) == 0x80)
            cp = (cp << 6) | (*s++ & 0x3F);

        if (cp < 0x100) out[n++] = (char)cp;
        else switch (cp)
        {
            case 0x2018: case 0x2019: case 0x2032: out[n++] = '\''; break;
            case 0x201C: case 0x201D: case 0x2033: out[n++] = '"'; break;
            case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212:
                out[n++] = '-'; break;
            case 0x2022: out[n++] = '*'; break;
            case 0x2026: out[n++] = '.'; out[n++] = '.'; out[n++] = '.'; break;
            case 0xFB00: out[n++] = 'f'; out[n++] = 'f'; break;
            case 0xFB01: out[n++] = 'f'; out[n++] = 'i'; break;
            case 0xFB02: out[n++] = 'f'; out[n++] = 'l'; break;
            case 0xFB03: out[n++] = 'f'; out[n++] = 'f'; out[n++] = 'i'; break;
            case 0xFB04: out[n++] = 'f'; out[n++] = 'f'; out[n++] = 'l'; break;
            default:     out[n++] = '?'; break;
        }
    }
    return n;
}

/* FTXT with CHRS for everyone and UTF8 for programs that know the chunk
 * (the convention used by the AROS SDL ports and VPDF). */
static int clipboard_put(const char *utf8)
{
    struct IFFHandle *iff;
    size_t ulen = strlen(utf8), llen;
    char *latin;
    int ok = 0;

    /* Every input byte yields at most three output bytes ("..."). */
    latin = malloc(ulen * 3 + 1);
    if (!latin)
        return 0;
    llen = utf8_to_latin1(utf8, latin);

    if ((iff = AllocIFF()))
    {
        if ((iff->iff_Stream = (IPTR)OpenClipboard(PRIMARY_CLIP)))
        {
            InitIFFasClip(iff);
            if (!OpenIFF(iff, IFFF_WRITE))
            {
                ok = !PushChunk(iff, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN)
                  && !PushChunk(iff, 0, ID_CHRS, IFFSIZE_UNKNOWN)
                  && WriteChunkBytes(iff, latin, llen) == (LONG)llen
                  && !PopChunk(iff)
                  && !PushChunk(iff, 0, ID_UTF8, IFFSIZE_UNKNOWN)
                  && WriteChunkBytes(iff, (APTR)utf8, ulen) == (LONG)ulen
                  && !PopChunk(iff)
                  && !PopChunk(iff);
                CloseIFF(iff);
            }
            CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
        }
        FreeIFF(iff);
    }
    free(latin);
    return ok;
}

/* --- Annotations and saving ------------------------------------------------- */

static void set_title(void)
{
    snprintf(title, sizeof(title), "Folio - %s%s", (char *)FilePart((CONST_STRPTR)doc_path),
             modified ? " (modified)" : "");
    if (win_obj)
        SET(win_obj, MUIA_Window_Title, (IPTR)title);
}

/* Forget the rendered image of a page so the next draw shows its new state. */
static void invalidate_page(int number)
{
    int k;
    for (k = 0; k < KIND_COUNT; k++)
    {
        struct CellData *e = INST_DATA(CellClass->mcc_Class, cols[k].cells[number]);
        /* Marking the size stale makes the next draw re-render; the old
         * image stays on screen until then, and the new one arrives in a
         * single blit. */
        e->boxW = -1;
        MUI_Redraw(cols[k].cells[number], MADF_DRAWOBJECT);
    }
}

/* One highlight annotation per page of the selection, from the same quads
 * the selection is drawn with. */
static void highlight_selection(void)
{
    static const float yellow[3] = { 1.0f, 0.9f, 0.2f };
    pdf_document *pdf = pdf_specifics(ctx, doc);
    fz_quad quads[512];
    int i, made = 0, first, last;

    if (sel_page < 0)
    {
        snprintf(status_text, sizeof(status_text), "select text first");
        update_label();
        return;
    }
    if (!pdf)
    {
        snprintf(status_text, sizeof(status_text), "not a PDF: cannot annotate");
        update_label();
        return;
    }
    /* Take the inversion off the screen and forget the selection before
     * any redraw, or the fresh draw paints the inversion back over the
     * new highlight. The span parameters are kept in locals. */
    {
        int f = sel_first(), l = sel_last();
        int sp = sel_page, ep = sel_end_page;
        fz_point pa = sel_a, pb = sel_b;
        /* The inversion is left on screen: the redraw below replaces the
         * whole cell in one blit, straight from inverted to highlighted. */
        drop_selection();
        sel_page = sp; sel_end_page = ep; sel_a = pa; sel_b = pb;  /* for sel_span() below */
        selecting = FALSE;
        first = f; last = l;
    }
    for (i = first; i <= last; i++)
    {
        fz_stext_page *text = stext_cached(i);
        fz_rect box = ((struct CellData *)INST_DATA(CellClass->mcc_Class, cols[KIND_MAIN].cells[i]))->box;
        fz_point a, b;
        pdf_page *page = NULL;
        pdf_annot *annot = NULL;
        int n;

        if (!text)
            continue;
        sel_span(i, &box, &a, &b);
        n = fz_highlight_selection(ctx, text, a, b, quads, 512);
        if (n < 1)
            continue;
        fz_var(page); fz_var(annot);
        fz_try(ctx)
        {
            page = pdf_load_page(ctx, pdf, i);
            annot = pdf_create_annot(ctx, page, PDF_ANNOT_HIGHLIGHT);
            pdf_set_annot_quad_points(ctx, annot, n, quads);
            pdf_set_annot_color(ctx, annot, 3, yellow);
            pdf_update_annot(ctx, annot);
            made++;
        }
        fz_always(ctx)
        {
            pdf_drop_annot(ctx, annot);
            pdf_drop_page(ctx, page);
        }
        fz_catch(ctx)
            fz_report_error(ctx);
    }
    sel_page = sel_end_page = -1;
    for (i = first; i <= last; i++)
        invalidate_page(i);
    if (made)
    {
        modified = TRUE;
        set_title();
        snprintf(status_text, sizeof(status_text), "highlighted on %d page%s, unsaved", made, made == 1 ? "" : "s");
    }
    else
        snprintf(status_text, sizeof(status_text), "nothing to highlight");
    drop_selection();
    update_label();
}

/* Save to path. Same file: incremental, the changes are appended and the
 * original bytes stay. Another file: a full rewrite. */
static int save_document(const char *path)
{
    pdf_document *pdf = pdf_specifics(ctx, doc);
    pdf_write_options opts = pdf_default_write_options;
    int same = !strcmp(path, doc_path), ok = 0;

    if (!pdf)
        return 0;
    opts.do_incremental = same && pdf_can_be_saved_incrementally(ctx, pdf);
    if (same && !opts.do_incremental)
    {
        snprintf(status_text, sizeof(status_text), "this file cannot be saved in place; use Save As");
        update_label();
        return 0;
    }
    fz_try(ctx)
    {
        pdf_save_document(ctx, pdf, path, &opts);
        ok = 1;
    }
    fz_catch(ctx)
    {
        fz_report_error(ctx);
        snprintf(status_text, sizeof(status_text), "save failed: %s", fz_caught_message(ctx));
    }
    if (ok)
    {
        modified = FALSE;
        snprintf(status_text, sizeof(status_text), "saved %s", (char *)FilePart((CONST_STRPTR)path));
        if (!same)
        {
            strncpy(doc_path, path, sizeof(doc_path) - 1);
            doc_path[sizeof(doc_path) - 1] = 0;
        }
        set_title();
    }
    update_label();
    return ok;
}

static int ask_save_file(char *path, size_t size)
{
    struct FileRequester *req;
    int ok = 0;

    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText,      (IPTR)"Folio: save PDF as",
        ASLFR_DoSaveMode,     TRUE,
        ASLFR_DoPatterns,     TRUE,
        ASLFR_InitialPattern, (IPTR)"#?.pdf",
        ASLFR_InitialFile,    (IPTR)FilePart((CONST_STRPTR)doc_path),
        TAG_DONE);
    if (!req)
        return 0;
    if (AslRequest(req, NULL) && req->fr_File && req->fr_File[0])
    {
        strncpy(path, (const char *)req->fr_Drawer, size - 1);
        path[size - 1] = 0;
        ok = AddPart((STRPTR)path, req->fr_File, size) ? 1 : 0;
    }
    FreeAslRequest(req);
    return ok;
}

static void save_as(void)
{
    static char chosen[512];
    if (ask_save_file(chosen, sizeof(chosen)))
        save_document(chosen);
}

/* Before discarding the document: 1 = go ahead, 0 = the user cancelled. */
static int confirm_discard(void)
{
    LONG r;
    if (!modified)
        return 1;
    r = MUI_Request(app_obj, win_obj, 0, (CONST_STRPTR)"Folio", (CONST_STRPTR)"_Save|_Discard|_Cancel",
                    (CONST_STRPTR)"%s has unsaved highlights.", (char *)FilePart((CONST_STRPTR)doc_path));
    if (r == 1)
        return save_document(doc_path) || (ask_save_file((char *)doc_path, sizeof(doc_path)) && save_document(doc_path));
    return r == 2;
}

/* Text of every page in the selection, joined with a line feed. Pages are
 * extracted one by one on the UI task; a long selection takes a moment. */
/* 24-bit ILBM, uncompressed: the picture format Amiga programs paste. Plane
 * order is red bits 0..7, then green, then blue; rows are padded to 16 px. */
static int clipboard_put_ilbm(fz_pixmap *pix)
{
    struct IFFHandle *iff;
    struct BitMapHeader bmh;
    LONG w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
    LONG stride = fz_pixmap_stride(ctx, pix), rowbytes = ((w + 15) / 16) * 2;
    const unsigned char *samples = fz_pixmap_samples(ctx, pix);
    unsigned char *row;
    int ok = 0;
    LONG y, p, x;

    row = calloc(rowbytes, 1);
    if (!row)
        return 0;
    memset(&bmh, 0, sizeof(bmh));
    bmh.bmh_Width = w; bmh.bmh_Height = h; bmh.bmh_Depth = 24;
    bmh.bmh_XAspect = bmh.bmh_YAspect = 1;
    bmh.bmh_PageWidth = w; bmh.bmh_PageHeight = h;

    if ((iff = AllocIFF()))
    {
        if ((iff->iff_Stream = (IPTR)OpenClipboard(PRIMARY_CLIP)))
        {
            InitIFFasClip(iff);
            if (!OpenIFF(iff, IFFF_WRITE))
            {
                ok = !PushChunk(iff, ID_ILBM, ID_FORM, IFFSIZE_UNKNOWN)
                  && !PushChunk(iff, 0, ID_BMHD, sizeof(bmh))
                  && WriteChunkBytes(iff, &bmh, sizeof(bmh)) == (LONG)sizeof(bmh)
                  && !PopChunk(iff)
                  && !PushChunk(iff, 0, ID_BODY, h * 24 * rowbytes);
                for (y = 0; ok && y < h; y++)
                {
                    const unsigned char *src = samples + y * stride;
                    for (p = 0; ok && p < 24; p++)
                    {
                        int channel = p / 8, bit = p % 8;
                        memset(row, 0, rowbytes);
                        for (x = 0; x < w; x++)
                            if (src[x * 3 + channel] & (1 << bit))
                                row[x / 8] |= 0x80 >> (x % 8);
                        ok = WriteChunkBytes(iff, row, rowbytes) == rowbytes;
                    }
                }
                ok = ok && !PopChunk(iff) && !PopChunk(iff);
                CloseIFF(iff);
            }
            CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
        }
        FreeIFF(iff);
    }
    free(row);
    return ok;
}

/* Render the rectangle between the drag's end points on the anchor page. */
static void copy_image(void)
{
    fz_pixmap *pix = NULL;
    fz_device *dev = NULL;
    fz_page *page = NULL;

    if (sel_page < 0 || sel_page != sel_end_page)
    {
        snprintf(status_text, sizeof(status_text), "select a rectangle on one page first");
        update_label();
        return;
    }
    fz_var(pix); fz_var(dev); fz_var(page);
    fz_try(ctx)
    {
        fz_matrix ctm = fz_scale(IMAGE_DPI / 72.0f, IMAGE_DPI / 72.0f);
        fz_rect r = fz_make_rect(fz_min(sel_a.x, sel_b.x), fz_min(sel_a.y, sel_b.y),
                                 fz_max(sel_a.x, sel_b.x), fz_max(sel_a.y, sel_b.y));
        fz_irect bbox = fz_round_rect(fz_transform_rect(r, ctm));
        if (bbox.x1 - bbox.x0 < 1 || bbox.y1 - bbox.y0 < 1)
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "empty selection");
        page = fz_load_page(ctx, doc, sel_page);
        pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), bbox, NULL, 0);
        fz_clear_pixmap_with_value(ctx, pix, 0xff);
        dev = fz_new_draw_device(ctx, fz_identity, pix);
        fz_run_page(ctx, page, dev, ctm, NULL);
        fz_close_device(ctx, dev);
        if (clipboard_put_ilbm(pix))
            snprintf(status_text, sizeof(status_text), "image %dx%d copied",
                     fz_pixmap_width(ctx, pix), fz_pixmap_height(ctx, pix));
        else
            snprintf(status_text, sizeof(status_text), "clipboard write failed");
        update_label();
    }
    fz_always(ctx)
    {
        fz_drop_device(ctx, dev);
        fz_drop_pixmap(ctx, pix);
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx)
    {
        fz_report_error(ctx);
        snprintf(status_text, sizeof(status_text), "render failed");
        update_label();
    }
}

static void copy_selection(void)
{
    fz_buffer *buf = NULL;
    int i;

    if (sel_page < 0)
        return;
    fz_var(buf);
    fz_try(ctx)
    {
        buf = fz_new_buffer(ctx, 1024);
        for (i = sel_first(); i <= sel_last(); i++)
        {
            fz_stext_page *text = stext_cached(i);
            fz_rect box = ((struct CellData *)INST_DATA(CellClass->mcc_Class, cols[KIND_MAIN].cells[i]))->box;
            fz_point a, b;
            char *part;
            if (!text)
                continue;
            sel_span(i, &box, &a, &b);
            part = fz_copy_selection(ctx, text, a, b, 0);
            if (part)
            {
                if (part[0])
                {
                    if (buf->len > 0)
                        fz_append_byte(ctx, buf, '\n');
                    fz_append_string(ctx, buf, part);
                }
                fz_free(ctx, part);
            }
        }
        if (buf->len > 0)
        {
            fz_terminate_buffer(ctx, buf);
            if (clipboard_put((const char *)buf->data))
                snprintf(status_text, sizeof(status_text), "%lu characters copied", (unsigned long)buf->len);
            else
                snprintf(status_text, sizeof(status_text), "clipboard write failed");
        }
        else
            snprintf(status_text, sizeof(status_text), "nothing to copy");
        update_label();
    }
    fz_always(ctx)
        fz_drop_buffer(ctx, buf);
    fz_catch(ctx)
    {
        fz_report_error(ctx);
        snprintf(status_text, sizeof(status_text), "copy failed: %s", fz_caught_message(ctx));
        update_label();
    }
}

/* --- Start-up ---------------------------------------------------------------- */

/* Ask for a file. Returns 1 and fills `path` when the user chose one. */
static int ask_file(char *path, size_t size)
{
    struct FileRequester *req;
    int ok = 0;

    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText,      (IPTR)"Folio: open PDF",
        ASLFR_DoPatterns,     TRUE,
        ASLFR_InitialPattern, (IPTR)"#?.pdf",
        ASLFR_RejectIcons,    TRUE,
        TAG_DONE);
    if (!req)
        return 0;
    if (AslRequest(req, NULL) && req->fr_File && req->fr_File[0])
    {
        strncpy(path, (const char *)req->fr_Drawer, size - 1);
        path[size - 1] = 0;
        ok = AddPart((STRPTR)path, req->fr_File, size) ? 1 : 0;
    }
    FreeAslRequest(req);
    return ok;
}

static int fill_column(int kind, Object *group)
{
    struct Column *c = &cols[kind];
    int i;

    c->group = group;
    c->cells = calloc(page_count, sizeof(*c->cells));
    if (!c->cells)
        return 0;
    for (i = 0; i < page_count; i++)
    {
        c->cells[i] = NewObject(CellClass->mcc_Class, NULL,
            MUIA_Cell_Page, i,
            MUIA_Cell_Kind, kind,
            MUIA_FillArea, FALSE,
            kind == KIND_THUMB ? MUIA_InputMode : TAG_IGNORE, MUIV_InputMode_RelVerify,
            kind == KIND_MAIN ? MUIA_ContextMenu : TAG_IGNORE, (IPTR)context_menu,
            TAG_DONE);
        if (!c->cells[i])
            return 0;
        DoMethod(group, OM_ADDMEMBER, (IPTR)c->cells[i]);
        if (kind == KIND_THUMB)
            DoMethod(c->cells[i], MUIM_Notify, MUIA_Pressed, FALSE,
                     (IPTR)reader_obj, 2, MUIM_Reader_Goto, i);
    }
    return 1;
}

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project",      NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Open...",      (STRPTR)"O", 0, 0, (APTR)MEN_OPEN },
    { NM_ITEM,  (STRPTR)"Save",         (STRPTR)"S", 0, 0, (APTR)MEN_SAVE },
    { NM_ITEM,  (STRPTR)"Save As...",   (STRPTR)"A", 0, 0, (APTR)MEN_SAVEAS },
    { NM_ITEM,  NM_BARLABEL,            NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"About...",     (STRPTR)"?", 0, 0, (APTR)MEN_ABOUT },
    { NM_ITEM,  (STRPTR)"About MUI...", NULL, 0, 0, (APTR)MEN_ABOUTMUI },
    { NM_ITEM,  NM_BARLABEL,            NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Quit",         (STRPTR)"Q", 0, 0, (APTR)MUIV_Application_ReturnID_Quit },
    { NM_TITLE, (STRPTR)"Edit",         NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Copy",         (STRPTR)"C", 0, 0, (APTR)MEN_COPY },
    { NM_ITEM,  (STRPTR)"Copy as Image", (STRPTR)"I", 0, 0, (APTR)MEN_COPYIMAGE },
    { NM_ITEM,  NM_BARLABEL,            NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Highlight",    (STRPTR)"H", 0, 0, (APTR)MEN_HIGHLIGHT },
    { NM_ITEM,  NM_BARLABEL,            NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Find...",      (STRPTR)"F", 0, 0, (APTR)MEN_FIND },
    { NM_ITEM,  (STRPTR)"Find Next",    (STRPTR)"G", 0, 0, (APTR)MEN_FINDNEXT },
    { NM_TITLE, (STRPTR)"View",         NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Zoom In",      (STRPTR)"=", 0, 0, (APTR)MEN_ZOOMIN },
    { NM_ITEM,  (STRPTR)"Zoom Out",     (STRPTR)"-", 0, 0, (APTR)MEN_ZOOMOUT },
    { NM_ITEM,  NM_BARLABEL,            NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Fit Width",    (STRPTR)"0", 0, 0, (APTR)MEN_FITWIDTH },
    { NM_ITEM,  (STRPTR)"Fit Page",     (STRPTR)"9", 0, 0, (APTR)MEN_FITPAGE },
    { NM_END,   NULL,                   NULL, 0, 0, NULL }
};

/* Right button over a page. Zune passes the item back; its UserData is the
 * same ID as in the main menu. */
static struct NewMenu context_menus[] = {
    { NM_TITLE, (STRPTR)"Page",          NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Copy",          NULL, 0, 0, (APTR)MEN_COPY },
    { NM_ITEM,  (STRPTR)"Copy as Image", NULL, 0, 0, (APTR)MEN_COPYIMAGE },
    { NM_ITEM,  (STRPTR)"Highlight",     NULL, 0, 0, (APTR)MEN_HIGHLIGHT },
    { NM_END,   NULL,                    NULL, 0, 0, NULL }
};

static const char *sidebar_titles[] = { "Pages", "Outline", NULL };

static const char about_text[] =
    "\33c\33bFolio 0.1\33n\n"
    "PDF reader for AROS\n\n"
    "Copyright (C) 2026 Tomasz Staniak\n"
    "Built on MuPDF " FZ_VERSION ", Copyright (C) Artifex Software, Inc.\n\n"
    "This program is free software under the GNU Affero General\n"
    "Public License, version 3 or later. It comes with NO WARRANTY.\n"
    "The source code is available from where you got this program.";

int main(int argc, char **argv)
{
    Object *app = NULL, *win, *prev, *next, *thumbs_group, *pages_group, *root;
    static char chosen[512];
    const char *path;
    int first_page = 0;
    int rc = RETURN_FAIL;

    const char *args[2] = { NULL, NULL };
    static char wbpath[512];
    int i, n = 0;

    if (argc == 0)
    {
        /* Workbench start: argv is the WBStartup; argument 0 is the program,
         * the rest are the project icons it was started with or dropped on. */
        struct WBStartup *wbs = (struct WBStartup *)argv;
        if (wbs && wbs->sm_NumArgs > 1 && wbs->sm_ArgList[1].wa_Lock &&
            NameFromLock(wbs->sm_ArgList[1].wa_Lock, (STRPTR)wbpath, sizeof(wbpath)))
        {
            if (!wbs->sm_ArgList[1].wa_Name ||
                AddPart((STRPTR)wbpath, (CONST_STRPTR)wbs->sm_ArgList[1].wa_Name, sizeof(wbpath)))
                args[0] = wbpath;
        }
    }
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-t"))
            show_timing = TRUE;
        else if (n < 2)
            args[n++] = argv[i];
    }

    if (args[0])
        path = args[0];
    else if (ask_file(chosen, sizeof(chosen)))
        path = chosen;
    else
        return RETURN_WARN;

    if (!open_document(path))
        goto out;
    if (args[1])
    {
        first_page = atoi(args[1]) - 1;
        if (first_page < 0) first_page = 0;
        if (first_page > page_count - 1) first_page = page_count - 1;
    }

    CellClass = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL,
                                      sizeof(struct CellData), CellDispatcher);
    ReaderClass = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Notify, NULL, 0, ReaderDispatcher);
    if (!CellClass || !ReaderClass)
        goto out;
    reader_obj = NewObject(ReaderClass->mcc_Class, NULL, TAG_DONE);
    context_menu = MUI_MakeObject(MUIO_MenustripNM, (IPTR)context_menus, 0);
    if (!reader_obj || !context_menu)
        goto out;

    snprintf(doc_path, sizeof(doc_path), "%s", path);
    set_title();
    update_label();

    app = ApplicationObject,
        MUIA_Application_Title,       (IPTR)"Folio",
        MUIA_Application_Version,     (IPTR)"$VER: Folio 0.1 (22.9.2026)",
        MUIA_Application_Description, (IPTR)"PDF reader on MuPDF",
        MUIA_Application_Base,        (IPTR)"FOLIO",
        SubWindow, (win = WindowObject,
            MUIA_Window_Title, (IPTR)title,
            MUIA_Window_ID, MAKE_ID('F','O','L','1'),
            MUIA_Window_AppWindow, TRUE,
            MUIA_Window_Menustrip, MUI_MakeObject(MUIO_MenustripNM, (IPTR)menus, 0),
            WindowContents, (root = VGroup,
                Child, (HGroup,
                    Child, (prev = SimpleButton("_Previous")),
                    Child, (page_label = TextObject,
                        MUIA_Frame, MUIV_Frame_Text,
                        MUIA_Text_Contents, (IPTR)label_text,
                    End),
                    Child, (next = SimpleButton("_Next")),
                End),
                Child, (HGroup,
                    Child, Label2("Find:"),
                    Child, (search_field = StringObject,
                        MUIA_Frame, MUIV_Frame_String,
                        MUIA_String_MaxLen, 256,
                        MUIA_CycleChain, 1,
                    End),
                End),
                Child, (HGroup,
                    /* The sidebar takes its share of the window and the
                     * user can drag the divider; cells follow the width. */
                    Child, (sidebar = RegisterObject,
                        MUIA_Register_Titles, (IPTR)sidebar_titles,
                        MUIA_HorizWeight, SIDEBAR_WEIGHT,
                        Child, (ScrollgroupObject,
                            MUIA_Scrollgroup_FreeHoriz, FALSE,
                            MUIA_Scrollgroup_Contents, (thumbs_group = VGroupV,
                                MUIA_Frame, MUIV_Frame_Virtual,
                                MUIA_Group_Spacing, cols[KIND_THUMB].spacing,
                            End),
                        End),
                        Child, (ListviewObject,
                            MUIA_Listview_List, (outline_list = ListObject,
                                MUIA_Frame, MUIV_Frame_InputList,
                                MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                                MUIA_List_DestructHook, MUIV_List_DestructHook_String,
                            End),
                        End),
                    End),
                    Child, (BalanceObject, End),
                    Child, (pages_scroll = ScrollgroupObject,
                        MUIA_Scrollgroup_FreeHoriz, TRUE,
                        MUIA_HorizWeight, 100,
                        MUIA_Scrollgroup_Contents, (pages_group = VGroupV,
                            MUIA_Frame, MUIV_Frame_Virtual,
                            MUIA_Group_Spacing, cols[KIND_MAIN].spacing,
                        End),
                    End),
                End),
            End),
        End),
    End;

    if (app)
    {
        ULONG sigs = 0;

        app_obj = app; win_obj = win;
        /* Zune sets MUIA_AppMessage on the root object for any drop. */
        DoMethod(outline_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime,
                 (IPTR)reader_obj, 1, MUIM_Reader_OutlinePick);
        fill_outline();
        DoMethod(root, MUIM_Notify, MUIA_AppMessage, MUIV_EveryTime,
                 (IPTR)reader_obj, 2, MUIM_Reader_Drop, MUIV_TriggerValue);
        if (!fill_column(KIND_THUMB, thumbs_group) || !fill_column(KIND_MAIN, pages_group))
        {
            fprintf(stderr, "Folio: out of memory building %d pages\n", page_count);
            MUI_DisposeObject(app);
            app = NULL;
            goto out;
        }

        DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
                 (IPTR)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
        DoMethod(prev, MUIM_Notify, MUIA_Pressed, FALSE,
                 (IPTR)reader_obj, 2, MUIM_Reader_Step, -1);
        DoMethod(next, MUIM_Notify, MUIA_Pressed, FALSE,
                 (IPTR)reader_obj, 2, MUIM_Reader_Step, 1);
        /* Return in the field searches; a new text starts from the current page. */
        DoMethod(search_field, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
                 (IPTR)reader_obj, 1, MUIM_Reader_Find);
        SET(win, MUIA_Window_Open, TRUE);
        layout_valid = TRUE;
        if (first_page > 0)
            DoMethod(app, MUIM_Application_PushMethod, (IPTR)reader_obj, 2,
                     MUIM_Reader_Goto, first_page);

        for (;;)
        {
            IPTR id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);

            if (id == (IPTR)MUIV_Application_ReturnID_Quit)
            {
                if (confirm_discard())
                    break;
                continue;
            }
            switch (id)
            {
                case MEN_ABOUT:
                    MUI_Request(app, win, 0, (CONST_STRPTR)"About Folio", (CONST_STRPTR)"*_OK", (CONST_STRPTR)about_text);
                    break;
                case MEN_ABOUTMUI:
                    DoMethod(app, MUIM_Application_AboutMUI, (IPTR)win);
                    break;
                case MEN_OPEN:
                    Reader_Open();
                    break;
                case MEN_SAVE:
                    if (modified) save_document(doc_path);
                    break;
                case MEN_SAVEAS:
                    save_as();
                    break;
                case MEN_HIGHLIGHT:
                    highlight_selection();
                    break;
                case MEN_COPY:
                    copy_selection();
                    break;
                case MEN_COPYIMAGE:
                    copy_image();
                    break;
                case MEN_FIND:
                    search_page = -1;
                    SET(win, MUIA_Window_ActiveObject, (IPTR)search_field);
                    break;
                case MEN_ZOOMIN:    DoMethod(reader_obj, MUIM_Reader_Zoom, ZOOM_IN); break;
                case MEN_ZOOMOUT:   DoMethod(reader_obj, MUIM_Reader_Zoom, ZOOM_OUT); break;
                case MEN_FITWIDTH:  DoMethod(reader_obj, MUIM_Reader_Zoom, ZOOM_FITWIDTH); break;
                case MEN_FITPAGE:   DoMethod(reader_obj, MUIM_Reader_Zoom, ZOOM_FITPAGE); break;
                case MEN_FINDNEXT:
                    DoMethod(reader_obj, MUIM_Reader_Find);
                    break;
            }
            if (sigs)
            {
                sigs = Wait(sigs | SIGBREAKF_CTRL_C);
                if (sigs & SIGBREAKF_CTRL_C) break;
            }
        }
        rc = RETURN_OK;
        layout_valid = FALSE;
        set_autoscroll(FALSE);
        drop_selection();
        drop_stext_cache();
        win_obj = NULL;
        MUI_DisposeObject(app);
    }

out:
    if (context_menu)
        MUI_DisposeObject(context_menu);
    if (reader_obj)
        DisposeObject(reader_obj);
    if (CellClass)
        MUI_DeleteCustomClass(CellClass);
    if (ReaderClass)
        MUI_DeleteCustomClass(ReaderClass);
    free(cols[KIND_THUMB].cells);
    free(cols[KIND_MAIN].cells);
    if (ctx)
    {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
    return rc;
}
