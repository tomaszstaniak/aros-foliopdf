/* Page column geometry: where each page cell sits, for both views.
 *
 * Single Page stacks one page per row, full column width. Magazine stacks
 * spreads: page 1 alone (the cover), then 2-3, 4-5 and so on side by side,
 * and a last odd page alone; a page alone is centred and as large as the
 * view allows. A spread is one row; single pages are rows of one, so the rest of
 * the program asks for spreads and never for rows.
 *
 * Every page has its own cell rectangle in column space (padding included),
 * and everything that maps between the screen and a page - drawing, hit
 * testing, scrolling, the zoom anchor, the render queue - reads it from
 * here. Pages are numbered from 0, so the cover is page 0 and the spreads
 * are (1,2), (3,4)... No MUI or MuPDF: tests/layout_test.c runs this on the
 * host. */

#ifndef FOLIO_LAYOUT_H
#define FOLIO_LAYOUT_H

enum { VIEW_SINGLE, VIEW_MAGAZINE };
enum { SIDE_FULL, SIDE_LEFT, SIDE_RIGHT };

/* Space between the two page images of a spread, the same as between two
 * rows (spacing plus the padding of both cells). */
#define MAG_GAP 16

struct PageGeom { LONG x, y, w, h; };   /* cell in column space, padding included */

static int spread_of(int mode, int page)
{
    return mode == VIEW_MAGAZINE ? (page + 1) / 2 : page;
}

static int spread_first(int mode, int s)
{
    return mode == VIEW_MAGAZINE && s > 0 ? 2 * s - 1 : s;
}

static int spread_last(int mode, int s, int n)
{
    int last = mode == VIEW_MAGAZINE && s > 0 ? 2 * s : s;
    return last < n - 1 ? last : n - 1;
}

static int spread_count(int mode, int n)
{
    return n > 0 ? spread_of(mode, n - 1) + 1 : 0;
}

static int page_side(int mode, int page)
{
    if (mode != VIEW_MAGAZINE)
        return SIDE_FULL;
    return page % 2 ? SIDE_LEFT : SIDE_RIGHT;   /* the cover, page 0, is a right page */
}

/* Width of one page image in a spread of content width w. */
static LONG layout_half(LONG w)
{
    LONG half = (w - MAG_GAP) / 2;
    return half < 1 ? 1 : half;
}

/* Lay out n pages for content width w (the column is w + 2 * pad wide).
 * aspect[i * stride bytes] is page i's height / width. Pages are scaled to
 * the width they get and top-aligned in their row. Returns the column
 * height.
 * A page alone in a Magazine spread (the cover, a last odd page) has no
 * partner to share the width with: it gets the whole content width.
 * Fit chooses that width from both viewport dimensions; Strip centres
 * the resulting column in the reading area. */
static LONG layout_column(int mode, int n, const float *aspect, int stride,
                          LONG w, LONG pad, LONG spacing, struct PageGeom *g)
{
    LONG y = 0, pw = mode == VIEW_MAGAZINE ? layout_half(w) : w;
    int s, count = spread_count(mode, n);

    for (s = 0; s < count; s++)
    {
        LONG row_h = 0;
        int i, lone = mode == VIEW_MAGAZINE && spread_first(mode, s) == spread_last(mode, s, n);
        for (i = spread_first(mode, s); i <= spread_last(mode, s, n); i++)
        {
            float a = *(const float *)((const char *)aspect + (long)i * stride);
            int side = page_side(mode, i);
            LONG this_w = pw;
            if (lone)
            {
                this_w = w;
                if (this_w < 1) this_w = 1;
            }
            g[i].w = this_w + 2 * pad;
            g[i].h = (LONG)(this_w * a) + 2 * pad;
            /* The right page is flush with the column's right edge, so an
             * odd pixel of rounding goes into the gap, not past the edge. */
            g[i].x = lone ? (w + 2 * pad - g[i].w) / 2
                          : side == SIDE_RIGHT ? w + 2 * pad - g[i].w : 0;
            g[i].y = y;
            if (g[i].h > row_h)
                row_h = g[i].h;
        }
        y += row_h + spacing;
    }
    return y > 0 ? y - spacing : 0;
}

/* Height of the row holding page `page`: the taller page of its spread. */
static LONG layout_row_h(int mode, int n, const struct PageGeom *g, int page)
{
    int s = spread_of(mode, page), i;
    LONG h = 0;
    for (i = spread_first(mode, s); i <= spread_last(mode, s, n); i++)
        if (g[i].h > h) h = g[i].h;
    return h;
}

/* The spread whose row, with the spacing below it, contains column row y. */
static int layout_spread_at(int mode, int n, const struct PageGeom *g, LONG y)
{
    int s, count = spread_count(mode, n);
    for (s = 0; s < count - 1; s++)
        if (y < g[spread_first(mode, s + 1)].y)
            break;
    return s;
}

/* The page whose cell contains (x, y), or -1. */
static int layout_hit(int mode, int n, const struct PageGeom *g, LONG x, LONG y)
{
    int s, i;
    if (n < 1)
        return -1;
    s = layout_spread_at(mode, n, g, y);
    for (i = spread_first(mode, s); i <= spread_last(mode, s, n); i++)
        if (x >= g[i].x && x < g[i].x + g[i].w && y >= g[i].y && y < g[i].y + g[i].h)
            return i;
    return -1;
}

/* The page whose cell is nearest to (x, y): for a drag that is in a gap,
 * beside a cover or under the shorter page of a spread. */
static int layout_nearest(int mode, int n, const struct PageGeom *g, LONG x, LONG y)
{
    int s, i, lo, hi, best = -1;
    double best_d = 0;
    if (n < 1)
        return -1;
    s = layout_spread_at(mode, n, g, y);
    lo = spread_first(mode, s);
    hi = spread_last(mode, s, n);
    /* Within a row the pointer stays on that row's pages, even where the
     * page above is closer (the empty half of the last spread). Between
     * rows the neighbours are enough: rows are stacked, so anything
     * further is further away. */
    if (y < g[lo].y || y >= g[lo].y + layout_row_h(mode, n, g, lo))
    {
        lo = spread_first(mode, s > 0 ? s - 1 : 0);
        hi = spread_last(mode, s + 1 < spread_count(mode, n) ? s + 1 : s, n);
    }
    for (i = lo; i <= hi; i++)
    {
        double dx = x < g[i].x ? g[i].x - x : (x >= g[i].x + g[i].w ? x - (g[i].x + g[i].w - 1) : 0);
        double dy = y < g[i].y ? g[i].y - y : (y >= g[i].y + g[i].h ? y - (g[i].y + g[i].h - 1) : 0);
        double d = dx * dx + dy * dy;
        if (best < 0 || d < best_d)
        {
            best = i;
            best_d = d;
        }
    }
    return best;
}

/* Magazine is paged: one spread fills the reading area and the others are
 * not shown at all. The vertical range the view may scroll through is that
 * spread's row; in Single Page it is the whole column. */
static void layout_scroll_range(int mode, int n, const struct PageGeom *g, LONG column_h,
                                int shown_spread, LONG *top, LONG *h)
{
    if (mode == VIEW_MAGAZINE && n > 0)
    {
        int first = spread_first(mode, shown_spread);
        *top = g[first].y;
        *h = layout_row_h(mode, n, g, first);
    }
    else
    {
        *top = 0;
        *h = column_h;
    }
}

/* A scroll offset kept inside the range; a range shorter than the view
 * sits at its top. */
static LONG layout_clamp_top(LONG range_top, LONG range_h, LONG view_h, LONG y)
{
    LONG max = range_top + range_h - view_h;
    if (max < range_top) max = range_top;
    return y < range_top ? range_top : (y > max ? max : y);
}

/* May page `page` be drawn at all while `shown_spread` is the current one? */
static int layout_page_shown(int mode, int shown_spread, int page)
{
    return mode != VIEW_MAGAZINE || spread_of(mode, page) == shown_spread;
}

/* The page Previous / Next (delta -1 / +1) goes to from `page`: the first
 * page of the neighbouring spread, or -1 at either end. One call is one
 * spread, whatever delta says, so one wheel event never skips several. */
static int layout_step(int mode, int n, int page, int delta)
{
    int s = spread_of(mode, page) + (delta < 0 ? -1 : 1);
    if (s < 0 || s > spread_count(mode, n) - 1)
        return -1;
    return spread_first(mode, s);
}

/* Content width at which the spread holding `page` fits a view whose
 * content area is vw x vh (padding already taken off), never wider than
 * vw: Fit Page. amax is the tallest aspect in that spread. A lone page
 * fits the same way as Single Page; its narrow column is centred by Strip. */
static LONG layout_fit_page_w(int mode, float amax, LONG vw, LONG vh, int lone)
{
    LONG w;
    if (amax <= 0 || vh < 1)
        return vw;
    if (mode == VIEW_MAGAZINE && !lone)
        w = 2 * (LONG)(vh / amax) + MAG_GAP;
    else
        w = (LONG)(vh / amax);
    if (w < 1) w = 1;
    return w < vw ? w : vw;
}

#endif
