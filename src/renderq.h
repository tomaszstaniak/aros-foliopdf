/* Render request queue: one record per page cell, in each column.
 *
 * A request is the full geometry a cell needs (page size and the band of
 * rows to rasterise). The state says where that request stands. The
 * functions here have no MuPDF or MUI dependencies so tests/queue_test.c
 * can drive them on the host; folio.c decides what is visible, whether
 * the image on hand covers the view, and does the rendering. */

#ifndef FOLIO_RENDERQ_H
#define FOLIO_RENDERQ_H

#define RENDER_RETRY_MS     5000    /* pause before a failed request is tried again */
#define RENDER_MAX_ATTEMPTS 3       /* then RS_FAILED until the user asks again */

struct RenderReq { LONG w, h, bandY, bandH; };

enum RenderState
{
    RS_READY,       /* the image on hand covers the view; nothing to do */
    RS_PENDING,     /* render at the next opportunity */
    RS_RETRY,       /* an attempt failed; render again once retry_at_ms has passed */
    RS_FAILED       /* RENDER_MAX_ATTEMPTS failed; wait for the user */
};

struct RenderQ
{
    struct RenderReq req;
    enum RenderState state;
    LONG retry_at_ms;
    int attempts;
};

static int renderq_same(const struct RenderReq *a, const struct RenderReq *b)
{
    return a->w == b->w && a->h == b->h && a->bandY == b->bandY && a->bandH == b->bandH;
}

/* Bring the queue in line with what the view needs now. `covered` says the
 * image on hand already serves the current geometry. */
static void renderq_update(struct RenderQ *q, int covered, const struct RenderReq *want)
{
    if (covered)
    {
        /* Whatever the old request was doing - pending, waiting to retry
         * or given up - it is obsolete. */
        q->state = RS_READY;
        q->attempts = 0;
        return;
    }
    if (!renderq_same(want, &q->req) || q->state == RS_READY)
    {
        /* A new request starts afresh. RS_READY without a covering image
         * is a cell whose image was invalidated: a new request as well. */
        q->req = *want;
        q->state = RS_PENDING;
        q->attempts = 0;
        q->retry_at_ms = 0;
    }
    /* Otherwise the same request stays where it was: pending, waiting for
     * its retry time, or failed for good. */
}

/* May this request be rendered now? */
static int renderq_due(const struct RenderQ *q, LONG now)
{
    /* The difference, not the comparison, so the millisecond clock may wrap. */
    return q->state == RS_PENDING ||
           (q->state == RS_RETRY && (int)((unsigned int)now - (unsigned int)q->retry_at_ms) >= 0);
}

/* Outcome of an attempt at q->req. */
static void renderq_done(struct RenderQ *q, int ok, LONG now)
{
    if (ok)
    {
        q->state = RS_READY;
        q->attempts = 0;
        return;
    }
    if (++q->attempts >= RENDER_MAX_ATTEMPTS)
        q->state = RS_FAILED;
    else
    {
        q->state = RS_RETRY;
        /* Unsigned, like the check in renderq_due(): the clock may wrap. */
        q->retry_at_ms = (LONG)((unsigned int)now + RENDER_RETRY_MS);
    }
}

/* The user asked for another go at a request that gave up. */
static int renderq_retry(struct RenderQ *q)
{
    if (q->state != RS_FAILED)
        return 0;
    q->state = RS_PENDING;
    q->attempts = 0;
    return 1;
}

#endif
