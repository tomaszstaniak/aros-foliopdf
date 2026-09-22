/* Host-side test of the render request queue (src/renderq.h): retry
 * timing, the attempt cap, request identity and user retry. Build and run
 * with scripts/test-queue.sh. */

#include <stdio.h>
#include <stdlib.h>

typedef int LONG;                   /* 32-bit, as on AROS x86_64 */
#include "../src/renderq.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; \
    fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const struct RenderReq A = { 800, 1100, 0, 900 };
static const struct RenderReq B = { 800, 1100, 600, 900 };   /* same size, other band */
static const struct RenderReq Z = { 1200, 1650, 0, 900 };    /* other zoom */

int main(void)
{
    struct RenderQ q = { { 0, 0, 0, 0 }, RS_READY, 0, 0 };
    LONG t = 1000;

    /* A cell with no image wants its request. */
    renderq_update(&q, 0, &A);
    CHECK(q.state == RS_PENDING && renderq_due(&q, t));

    /* Success: ready, nothing due. A covering image keeps it so. */
    renderq_done(&q, 1, t);
    CHECK(q.state == RS_READY && !renderq_due(&q, t));
    renderq_update(&q, 1, &A);
    CHECK(q.state == RS_READY);

    /* The view moves off the band: a new request, pending. */
    renderq_update(&q, 0, &B);
    CHECK(q.state == RS_PENDING && renderq_same(&q.req, &B));

    /* First failure: waiting, not due until RENDER_RETRY_MS has passed,
     * and the tick in between must not start it again. */
    renderq_done(&q, 0, t);
    CHECK(q.state == RS_RETRY && q.attempts == 1);
    CHECK(!renderq_due(&q, t));
    CHECK(!renderq_due(&q, t + RENDER_RETRY_MS - 1));
    CHECK(renderq_due(&q, t + RENDER_RETRY_MS));

    /* Re-deriving the same request while waiting keeps the wait: no
     * scroll or redraw is needed for the retry, and none resets it. */
    renderq_update(&q, 0, &B);
    CHECK(q.state == RS_RETRY && q.attempts == 1 && !renderq_due(&q, t + 1));

    /* Second failure, then a third: failed for good, never due. */
    t += RENDER_RETRY_MS;
    renderq_done(&q, 0, t);
    CHECK(q.state == RS_RETRY && q.attempts == 2);
    t += RENDER_RETRY_MS;
    CHECK(renderq_due(&q, t));
    renderq_done(&q, 0, t);
    CHECK(q.state == RS_FAILED && q.attempts == RENDER_MAX_ATTEMPTS);
    CHECK(!renderq_due(&q, t + 10 * RENDER_RETRY_MS));
    renderq_update(&q, 0, &B);
    CHECK(q.state == RS_FAILED);

    /* The user asks again: a fresh set of attempts. */
    CHECK(renderq_retry(&q) == 1);
    CHECK(q.state == RS_PENDING && q.attempts == 0 && renderq_due(&q, t));
    CHECK(renderq_retry(&q) == 0);      /* nothing failed now */

    /* A failing request is dropped as soon as the view changes: a zoom
     * makes a new pending request with a clean count... */
    renderq_done(&q, 0, t);
    renderq_done(&q, 0, t);
    CHECK(q.state == RS_RETRY && q.attempts == 2);
    renderq_update(&q, 0, &Z);
    CHECK(q.state == RS_PENDING && q.attempts == 0 && renderq_same(&q.req, &Z));

    /* ...and so does the old image covering the view again, even from
     * RS_FAILED. */
    renderq_done(&q, 0, t); renderq_done(&q, 0, t); renderq_done(&q, 0, t);
    CHECK(q.state == RS_FAILED);
    renderq_update(&q, 1, &Z);
    CHECK(q.state == RS_READY && q.attempts == 0);

    /* An invalidated image (ready, but no longer covering) re-requests
     * the same geometry. */
    renderq_update(&q, 0, &Z);
    CHECK(q.state == RS_PENDING);

    /* Retry timing survives a wrapping millisecond clock. */
    q.state = RS_RETRY; q.retry_at_ms = (LONG)(0x7fffffffu - 100 + RENDER_RETRY_MS);
    CHECK(!renderq_due(&q, 0x7fffffff - 100));
    CHECK(renderq_due(&q, (LONG)(0x7fffffffu - 100 + RENDER_RETRY_MS)));

    if (failures)
    {
        fprintf(stderr, "queue_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("queue_test: ok");
    return 0;
}
