/* Host-side test of the page column geometry (src/layout.h): page pairing
 * in Magazine, cell placement for pages of different proportions, hit
 * testing, the row at a scroll offset and Fit Page. Build and run with
 * scripts/test-layout.sh. */

#include <stdio.h>
#include <stdlib.h>

typedef int LONG;                   /* 32-bit, as on AROS x86_64 */
#include "../src/layout.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; \
    fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define PAD 6
#define SPACING 4
#define W 600                       /* content width; the column is W + 2 * PAD */

static struct PageGeom g[16];

static LONG lay(int mode, int n, const float *a)
{
    return layout_column(mode, n, a, sizeof(float), W, PAD, SPACING, g);
}

static void test_pairing(void)
{
    int n;
    /* Single Page: every page is its own spread. */
    for (n = 1; n <= 6; n++)
        CHECK(spread_count(VIEW_SINGLE, n) == n);
    CHECK(spread_of(VIEW_SINGLE, 4) == 4 && spread_first(VIEW_SINGLE, 4) == 4 && spread_last(VIEW_SINGLE, 4, 6) == 4);
    CHECK(page_side(VIEW_SINGLE, 0) == SIDE_FULL);

    /* Magazine: cover alone on the right, then (1,2), (3,4)... */
    CHECK(spread_count(VIEW_MAGAZINE, 1) == 1);
    CHECK(spread_count(VIEW_MAGAZINE, 2) == 2);
    CHECK(spread_count(VIEW_MAGAZINE, 3) == 2);
    CHECK(spread_count(VIEW_MAGAZINE, 6) == 4);
    CHECK(page_side(VIEW_MAGAZINE, 0) == SIDE_RIGHT);
    CHECK(page_side(VIEW_MAGAZINE, 1) == SIDE_LEFT && page_side(VIEW_MAGAZINE, 2) == SIDE_RIGHT);
    CHECK(page_side(VIEW_MAGAZINE, 5) == SIDE_LEFT);
    CHECK(spread_of(VIEW_MAGAZINE, 0) == 0);
    CHECK(spread_of(VIEW_MAGAZINE, 1) == 1 && spread_of(VIEW_MAGAZINE, 2) == 1);
    CHECK(spread_of(VIEW_MAGAZINE, 3) == 2 && spread_of(VIEW_MAGAZINE, 4) == 2);
    CHECK(spread_of(VIEW_MAGAZINE, 5) == 3);
    CHECK(spread_first(VIEW_MAGAZINE, 0) == 0 && spread_last(VIEW_MAGAZINE, 0, 6) == 0);
    CHECK(spread_first(VIEW_MAGAZINE, 1) == 1 && spread_last(VIEW_MAGAZINE, 1, 6) == 2);
    /* Six pages: the last spread is page 5 alone, on the left. */
    CHECK(spread_first(VIEW_MAGAZINE, 3) == 5 && spread_last(VIEW_MAGAZINE, 3, 6) == 5);
    /* Two pages: the second spread has only its left page. */
    CHECK(spread_first(VIEW_MAGAZINE, 1) == 1 && spread_last(VIEW_MAGAZINE, 1, 2) == 1);
    /* Every page is in the spread spread_of() names. */
    for (n = 1; n <= 9; n++)
    {
        int p;
        for (p = 0; p < n; p++)
        {
            int s = spread_of(VIEW_MAGAZINE, p);
            CHECK(s < spread_count(VIEW_MAGAZINE, n));
            CHECK(p >= spread_first(VIEW_MAGAZINE, s) && p <= spread_last(VIEW_MAGAZINE, s, n));
        }
    }
}

static void test_single_layout(void)
{
    static const float a[3] = { 1.414f, 0.5f, 1.0f };
    LONG h = lay(VIEW_SINGLE, 3, a);
    /* Unchanged from the one-column layout: full width, stacked. */
    CHECK(g[0].x == 0 && g[0].w == W + 2 * PAD && g[0].y == 0);
    CHECK(g[0].h == (LONG)(W * 1.414f) + 2 * PAD);
    CHECK(g[1].y == g[0].h + SPACING && g[1].h == W / 2 + 2 * PAD);
    CHECK(g[2].y == g[1].y + g[1].h + SPACING);
    CHECK(h == g[2].y + g[2].h);
    CHECK(layout_spread_at(VIEW_SINGLE, 3, g, g[1].y) == 1);
    CHECK(layout_spread_at(VIEW_SINGLE, 3, g, g[1].y - 1) == 0);     /* the spacing belongs above */
}

static void test_magazine_layout(void)
{
    static const float a1[1] = { 1.414f };
    static const float a6[6] = { 1.414f, 1.414f, 0.7f, 1.414f, 1.414f, 1.414f };
    LONG half = layout_half(W), h;
    int i;

    CHECK(half == (W - MAG_GAP) / 2);

    /* One page: the cover alone takes the whole width (no height limit
     * here; see test_lone_pages), flush with the right edge. */
    h = lay(VIEW_MAGAZINE, 1, a1);
    CHECK(g[0].w == W + 2 * PAD && g[0].x + g[0].w == W + 2 * PAD && g[0].y == 0);
    CHECK(h == g[0].h);

    /* Six pages, one of them landscape (page 2, the right of spread 1). */
    h = lay(VIEW_MAGAZINE, 6, a6);
    CHECK(g[0].x + g[0].w == W + 2 * PAD);              /* cover: against the right edge */
    CHECK(g[1].x == 0 && g[2].x + g[2].w == W + 2 * PAD);   /* 1 left, 2 right */
    CHECK(g[1].w == half + 2 * PAD);                    /* a pair shares the width */
    CHECK(g[1].y == g[2].y && g[1].y == g[0].h + SPACING);
    /* Both halves get the same width; the landscape page is scaled to it,
     * not stretched, and top-aligned with its partner. */
    CHECK(g[1].w == g[2].w);
    CHECK(g[2].h == (LONG)(half * 0.7f) + 2 * PAD && g[2].h < g[1].h);
    /* The row is as tall as its taller page. */
    CHECK(layout_row_h(VIEW_MAGAZINE, 6, g, 2) == g[1].h);
    CHECK(g[3].y == g[1].y + g[1].h + SPACING && g[4].y == g[3].y);
    /* Last spread: page 5 alone, on the left. */
    CHECK(g[5].x == 0 && g[5].y == g[3].y + g[3].h + SPACING);
    CHECK(h == g[5].y + g[5].h);
    /* The gap between the page images of a spread is MAG_GAP. */
    CHECK((g[2].x + PAD) - (g[1].x + g[1].w - PAD) == MAG_GAP);
    /* No page leaves the column. */
    for (i = 0; i < 6; i++)
        CHECK(g[i].x >= 0 && g[i].x + g[i].w <= W + 2 * PAD);

    /* Rows at scroll offsets. */
    CHECK(layout_spread_at(VIEW_MAGAZINE, 6, g, 0) == 0);
    CHECK(layout_spread_at(VIEW_MAGAZINE, 6, g, g[1].y) == 1);
    CHECK(layout_spread_at(VIEW_MAGAZINE, 6, g, g[3].y - 1) == 1);
    CHECK(layout_spread_at(VIEW_MAGAZINE, 6, g, h + 1000) == 3);

    /* Hit testing uses each page's own rectangle. */
    CHECK(layout_hit(VIEW_MAGAZINE, 6, g, g[0].x + 5, 5) == 0);
    CHECK(layout_hit(VIEW_MAGAZINE, 6, g, 10, g[1].y + 10) == 1);
    CHECK(layout_hit(VIEW_MAGAZINE, 6, g, g[2].x + 10, g[2].y + 10) == 2);
    /* Under the short landscape page: nothing there... */
    CHECK(layout_hit(VIEW_MAGAZINE, 6, g, g[2].x + 10, g[2].y + g[2].h + 5) == -1);
    /* ...and a drag there snaps to the landscape page above, not across. */
    CHECK(layout_nearest(VIEW_MAGAZINE, 6, g, g[2].x + 10, g[2].y + g[2].h + 5) == 2);
    /* In the spine gap: the nearer side. */
    CHECK(layout_nearest(VIEW_MAGAZINE, 6, g, g[1].x + g[1].w + 1, g[1].y + 20) == 1);
    CHECK(layout_nearest(VIEW_MAGAZINE, 6, g, g[2].x - 1, g[1].y + 20) == 2);
    /* Beside the cover, and in the empty right half of the last spread. */
    CHECK(layout_nearest(VIEW_MAGAZINE, 6, g, 5, 5) == 0);
    CHECK(layout_nearest(VIEW_MAGAZINE, 6, g, W, g[5].y + 5) == 5);
    /* Between two rows: the one it is closer to. */
    CHECK(layout_nearest(VIEW_MAGAZINE, 6, g, 10, g[3].y - 1) == 3);
}

static void test_three_pages(void)
{
    static const float a[3] = { 1.3f, 1.3f, 1.3f };
    lay(VIEW_MAGAZINE, 3, a);
    /* Cover, then 1 and 2 side by side: a jump to page 2 (the right page)
     * lands on the row that shows both. */
    CHECK(spread_count(VIEW_MAGAZINE, 3) == 2);
    CHECK(g[2].y == g[1].y && g[2].x > g[1].x);
    CHECK(layout_spread_at(VIEW_MAGAZINE, 3, g, g[2].y) == spread_of(VIEW_MAGAZINE, 2));
    CHECK(spread_first(VIEW_MAGAZINE, spread_of(VIEW_MAGAZINE, 2)) == 1);
}

static void test_two_pages(void)
{
    static const float a[2] = { 1.414f, 1.414f };
    LONG h = lay(VIEW_MAGAZINE, 2, a);
    CHECK(g[1].x == 0 && g[1].y > g[0].y);
    CHECK(h == g[1].y + g[1].h);
}

static void test_fit_page(void)
{
    /* Single: the page height fits the view. */
    CHECK(layout_fit_page_w(VIEW_SINGLE, 1.5f, 600, 300, 0) == 200);
    /* Magazine: two halves of that width plus the gap. */
    CHECK(layout_fit_page_w(VIEW_MAGAZINE, 1.5f, 600, 300, 0) == 2 * 200 + MAG_GAP);
    /* Never wider than the view: a spread that is short already fits. */
    CHECK(layout_fit_page_w(VIEW_MAGAZINE, 0.5f, 600, 400, 0) == 600);
    /* The result lays out to a row that fits the height. */
    {
        static const float a[3] = { 1.5f, 1.5f, 1.2f };
        LONG w = layout_fit_page_w(VIEW_MAGAZINE, 1.5f, 1000, 300, 0);
        layout_column(VIEW_MAGAZINE, 3, a, sizeof(float), w, PAD, SPACING, g);
        CHECK(layout_row_h(VIEW_MAGAZINE, 3, g, 1) - 2 * PAD <= 300);
        CHECK(layout_row_h(VIEW_MAGAZINE, 3, g, 1) - 2 * PAD >= 297);
    }
}

static void test_paged_magazine(void)
{
    static const float a[6] = { 1.414f, 1.414f, 0.7f, 1.414f, 1.414f, 1.414f };
    LONG colh = lay(VIEW_MAGAZINE, 6, a), top, h;
    int p, i, s;

    /* Wheel and keys: one step is one spread, forwards and back, and stops
     * at the ends. From the right page of a pair the step is still one. */
    CHECK(layout_step(VIEW_MAGAZINE, 6, 0, 1) == 1);
    CHECK(layout_step(VIEW_MAGAZINE, 6, 1, 1) == 3);
    CHECK(layout_step(VIEW_MAGAZINE, 6, 2, 1) == 3);
    CHECK(layout_step(VIEW_MAGAZINE, 6, 4, 1) == 5);
    CHECK(layout_step(VIEW_MAGAZINE, 6, 5, 1) == -1);
    CHECK(layout_step(VIEW_MAGAZINE, 6, 4, -1) == 1);
    CHECK(layout_step(VIEW_MAGAZINE, 6, 1, -1) == 0);
    CHECK(layout_step(VIEW_MAGAZINE, 6, 0, -1) == -1);
    CHECK(layout_step(VIEW_MAGAZINE, 6, 1, 5) == 3);       /* never more than one */
    CHECK(layout_step(VIEW_SINGLE, 6, 2, 1) == 3);

    /* Walking the whole document by steps visits every spread once. */
    for (p = 0, s = 1; (p = layout_step(VIEW_MAGAZINE, 6, p, 1)) >= 0; s++)
        ;
    CHECK(s == spread_count(VIEW_MAGAZINE, 6));

    /* Neighbouring spreads are never shown. */
    for (s = 0; s < spread_count(VIEW_MAGAZINE, 6); s++)
        for (i = 0; i < 6; i++)
            CHECK(layout_page_shown(VIEW_MAGAZINE, s, i) == (spread_of(VIEW_MAGAZINE, i) == s));
    CHECK(layout_page_shown(VIEW_SINGLE, 0, 5));

    /* The scroll range is the shown spread's row only... */
    layout_scroll_range(VIEW_MAGAZINE, 6, g, colh, 1, &top, &h);
    CHECK(top == g[1].y && h == g[1].h);
    /* ...so no offset can bring a neighbour's row into a view as tall as
     * the row, or reveal it when the row is shorter than the view. */
    CHECK(layout_clamp_top(top, h, h, 0) == top);
    CHECK(layout_clamp_top(top, h, h, colh) == top);
    CHECK(layout_clamp_top(top, h, 2 * h, top + 50) == top);
    /* Zoomed in (row taller than the view): movement inside the row only. */
    CHECK(layout_clamp_top(top, h, h / 2, top + 10) == top + 10);
    CHECK(layout_clamp_top(top, h, h / 2, colh) == top + h - h / 2);
    CHECK(layout_clamp_top(top, h, h / 2, -100) == top);
    /* Single Page: the whole column. */
    layout_scroll_range(VIEW_SINGLE, 6, g, colh, 3, &top, &h);
    CHECK(top == 0 && h == colh);

    /* A jump to the right page of a pair shows that pair. */
    CHECK(spread_of(VIEW_MAGAZINE, 4) == 2);
    layout_scroll_range(VIEW_MAGAZINE, 6, g, colh, spread_of(VIEW_MAGAZINE, 4), &top, &h);
    CHECK(top == g[3].y && top == g[4].y);
}

static void test_fit_tall_page(void)
{
    /* A very tall spread in a wide, low window: the height decides, and
     * the fitted row is no taller than the view. */
    static const float a[3] = { 1.0f, 3.0f, 1.0f };
    LONG w = layout_fit_page_w(VIEW_MAGAZINE, 3.0f, 1200, 450, 0);
    CHECK(w == 2 * 150 + MAG_GAP);
    layout_column(VIEW_MAGAZINE, 3, a, sizeof(float), w, PAD, SPACING, g);
    CHECK(layout_row_h(VIEW_MAGAZINE, 3, g, 1) - 2 * PAD <= 450);
    CHECK(g[1].h > g[2].h && g[1].y == g[2].y);     /* the other page top-aligned */
}

static void test_lone_pages(void)
{
    static const float a[6] = { 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f };
    LONG w = layout_fit_page_w(VIEW_MAGAZINE, 1.5f, W, 300, 1);
    LONG screen_left;
    layout_column(VIEW_MAGAZINE, 6, a, sizeof(float), w, PAD, SPACING, g);
    CHECK(g[0].w == 200 + 2 * PAD && g[0].h == 300 + 2 * PAD);
    /* Strip centres a column narrower than its viewport. Both lone pages
     * must occupy that centred column, with equal outside margins. */
    screen_left = (W + 2 * PAD - (w + 2 * PAD)) / 2;
    CHECK(screen_left + g[0].x == (W + 2 * PAD - g[0].w) / 2);
    CHECK(g[5].x == g[0].x && g[5].w == g[0].w);
    CHECK(layout_hit(VIEW_MAGAZINE, 6, g, g[0].w + 5, 5) == -1);
    CHECK(layout_nearest(VIEW_MAGAZINE, 6, g, g[0].w + 5, 5) == 0);
    /* Changing to a pair refits that spread against the same viewport. */
    w = layout_fit_page_w(VIEW_MAGAZINE, 1.5f, W, 300, 0);
    layout_column(VIEW_MAGAZINE, 6, a, sizeof(float), w, PAD, SPACING, g);
    CHECK(g[1].w == 200 + 2 * PAD && g[2].w == g[1].w);
    {
        static const float land[1] = { 0.5f };
        w = layout_fit_page_w(VIEW_MAGAZINE, land[0], W, 1000, 1);
        layout_column(VIEW_MAGAZINE, 1, land, sizeof(float), w, PAD, SPACING, g);
        CHECK(g[0].w == W + 2 * PAD && g[0].h == W / 2 + 2 * PAD);
    }
    CHECK(layout_fit_page_w(VIEW_MAGAZINE, 1.5f, 600, 300, 1) == 200);
    CHECK(layout_fit_page_w(VIEW_SINGLE, 1.5f, 600, 300, 1) == 200);
}

static void test_lone_fit_zoom_transition(void)
{
    static const float a[1] = { 1.5f };
    LONG fitted = layout_fit_page_w(VIEW_MAGAZINE, a[0], 600, 300, 1);
    /* The fitted width must itself encode the visible scale, so disabling
     * fit mode for the first manual zoom cannot enlarge a Zoom Out. */
    layout_column(VIEW_MAGAZINE, 1, a, sizeof(float), fitted, PAD, SPACING, g);
    CHECK(g[0].h - 2 * PAD <= 300);
    CHECK(fitted == 200);
    layout_column(VIEW_MAGAZINE, 1, a, sizeof(float), (LONG)(fitted / 1.25f), PAD, SPACING, g);
    CHECK(g[0].w - 2 * PAD == 160 && g[0].h - 2 * PAD == 240);
}

int main(void)
{
    test_lone_fit_zoom_transition();
    test_pairing();
    test_single_layout();
    test_magazine_layout();
    test_three_pages();
    test_two_pages();
    test_fit_page();
    test_paged_magazine();
    test_fit_tall_page();
    test_lone_pages();
    if (failures)
    {
        fprintf(stderr, "layout_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("layout_test: all checks passed\n");
    return 0;
}
