/* Host-side test of the render worker (src/renderworker.h) with fake
 * render callbacks: results, aborts, a document change while a job runs,
 * an open failure, the single job slot, and stopping while a job runs.
 * Build and run with scripts/test-worker.sh (also under ThreadSanitizer). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

typedef int LONG;

static int live_buffers;
static void counted_free(void *p) { if (p) live_buffers--; free(p); }
static void *counted_malloc(size_t n) { void *p = malloc(n); if (p) live_buffers++; return p; }

#define free counted_free
#include "../src/renderq.h"
#include "../src/renderworker.h"
#undef free

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; \
    fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); } } while (0)

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* The fake worker context: a document name and an abort flag, as the
 * real one has a document and a fz_cookie. */
struct Fake { char doc[64]; int abort; int opens, closes; };

static pthread_mutex_t note_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t note_cond = PTHREAD_COND_INITIALIZER;
static int notes;
static int fini_called;

static void *f_init(void *user) { (void)user; return calloc(1, sizeof(struct Fake)); }
static void f_fini(void *wctx) { fini_called++; free(wctx); }
static int f_open(void *wctx, const char *path, char *msg, size_t n)
{
    struct Fake *f = wctx;
    f->opens++;
    if (strstr(path, "bad")) { snprintf(msg, n, "cannot open %s", path); return 0; }
    snprintf(f->doc, sizeof(f->doc), "%s", path);
    return 1;
}
static void f_close(void *wctx) { struct Fake *f = wctx; if (f->doc[0]) f->closes++; f->doc[0] = 0; }
/* The abort flag is the one word both threads touch without the lock, as
 * MuPDF polls fz_cookie.abort; atomic stores and loads say so to TSan. */
static void f_prepare(void *wctx) { __atomic_store_n(&((struct Fake *)wctx)->abort, 0, __ATOMIC_RELAXED); }
static void f_abort(void *wctx) { __atomic_store_n(&((struct Fake *)wctx)->abort, 1, __ATOMIC_RELAXED); }
static int f_aborted(struct Fake *f) { return __atomic_load_n(&f->abort, __ATOMIC_RELAXED); }
/* Takes job->w milliseconds, checking the abort flag every millisecond
 * like MuPDF's interpreter checks the cookie between operators. A page
 * number of 99 fails. */
static void f_render(void *wctx, const struct RwJob *job, struct RwResult *res)
{
    struct Fake *f = wctx;
    long t0 = now_ms(), i;
    for (i = 0; i < job->w && !f_aborted(f); i++)
        sleep_ms(1);
    res->ms = now_ms() - t0;
    if (f_aborted(f)) { res->outcome = RO_ABORTED; return; }
    if (job->page == 99) { res->outcome = RO_FAILED; snprintf(res->msg, sizeof(res->msg), "bad page"); return; }
    res->rgb = counted_malloc(16);
    res->w = 2; res->h = 2; res->stride = 6;
    res->outcome = RO_OK;
}
static void f_notify(void *user)
{
    (void)user;
    pthread_mutex_lock(&note_lock);
    notes++;
    pthread_cond_signal(&note_cond);
    pthread_mutex_unlock(&note_lock);
}

/* Wait up to `limit` ms for a result; returns 1 and fills res. */
static int wait_result(struct RenderWorker *w, struct RwResult *res, long limit)
{
    long end = now_ms() + limit;
    while (now_ms() < end)
    {
        if (rw_take_result(w, res))
            return 1;
        pthread_mutex_lock(&note_lock);
        if (!notes)
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 5000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&note_cond, &note_lock, &ts);
        }
        notes = 0;
        pthread_mutex_unlock(&note_lock);
    }
    return 0;
}

static struct RwJob job(unsigned int id, unsigned int gen, int page, LONG ms)
{
    struct RwJob j;
    memset(&j, 0, sizeof(j));
    j.id = id; j.doc_gen = gen; j.page = page; j.w = ms; j.h = 10; j.bandH = 10;
    return j;
}

static const struct RwCallbacks cbs = {
    f_init, NULL, f_fini, f_open, f_close, f_prepare, f_abort, f_render, f_notify, NULL
};

int main(void)
{
    struct RenderWorker w;
    struct RwResult r;
    struct RwJob j;
    long t0;
    unsigned int id = 0;

    CHECK(rw_start(&w, &cbs, 1 << 20));
    rw_set_document(&w, 1, "a.pdf");

    /* An ordinary job: an image, owned by the taker. */
    j = job(++id, 1, 0, 10);
    CHECK(rw_idle(&w));
    CHECK(rw_submit(&w, &j));
    CHECK(!rw_idle(&w));
    /* One job at a time: a second one is refused until the first is answered. */
    {
        struct RwJob k = job(++id, 1, 1, 10);
        CHECK(!rw_submit(&w, &k));
    }
    CHECK(wait_result(&w, &r, 2000));
    CHECK(r.outcome == RO_OK && r.job.id == j.id && r.rgb != NULL);
    counted_free(r.rgb);
    CHECK(rw_idle(&w));

    /* Abort the running job: an answer at once, an abort, no image. */
    j = job(++id, 1, 0, 5000);
    CHECK(rw_submit(&w, &j));
    sleep_ms(30);
    CHECK(rw_current(&w, NULL) == j.id);
    t0 = now_ms();
    CHECK(!rw_abort(&w, j.id + 1000));  /* another job's abort does nothing */
    CHECK(rw_abort(&w, j.id));
    CHECK(!rw_abort(&w, j.id));         /* sent once */
    CHECK(wait_result(&w, &r, 2000));
    CHECK(now_ms() - t0 < 500);
    CHECK(r.outcome == RO_ABORTED && r.rgb == NULL && r.job.id == j.id);

    /* A document change while a job runs aborts it; a job still made for
     * the old document afterwards is answered without rendering. */
    j = job(++id, 1, 0, 5000);
    CHECK(rw_submit(&w, &j));
    sleep_ms(30);
    rw_set_document(&w, 2, "b.pdf");
    CHECK(wait_result(&w, &r, 2000));
    CHECK(r.outcome == RO_ABORTED && r.job.doc_gen == 1);
    j = job(++id, 1, 0, 50);
    CHECK(rw_submit(&w, &j));
    t0 = now_ms();
    CHECK(wait_result(&w, &r, 2000));
    CHECK(r.outcome == RO_ABORTED && now_ms() - t0 < 40);
    j = job(++id, 2, 0, 10);
    CHECK(rw_submit(&w, &j));
    CHECK(wait_result(&w, &r, 2000));
    CHECK(r.outcome == RO_OK);
    counted_free(r.rgb);

    /* A render failure and an open failure are failures with a reason. */
    j = job(++id, 2, 99, 1);
    CHECK(rw_submit(&w, &j));
    CHECK(wait_result(&w, &r, 2000));
    CHECK(r.outcome == RO_FAILED && !strcmp(r.msg, "bad page"));
    rw_set_document(&w, 3, "bad.pdf");
    j = job(++id, 3, 0, 1);
    CHECK(rw_submit(&w, &j));
    CHECK(wait_result(&w, &r, 2000));
    CHECK(r.outcome == RO_FAILED && strstr(r.msg, "cannot open") != NULL);
    /* Reopening the same path under a new generation (after a save). */
    rw_set_document(&w, 4, "a.pdf");
    j = job(++id, 4, 0, 1);
    CHECK(rw_submit(&w, &j));
    CHECK(wait_result(&w, &r, 2000));
    CHECK(r.outcome == RO_OK);
    counted_free(r.rgb);

    /* The UI side: an answer is used only for the job the record waits for. */
    {
        struct RenderQ q;
        struct RenderReq a = { 800, 1100, 0, 900 }, b = { 800, 1100, 600, 900 };
        memset(&q, 0, sizeof(q));
        renderq_update(&q, 0, &a);
        renderq_start(&q, 7);
        CHECK(q.state == RS_RUNNING && !renderq_due(&q, 0));
        renderq_update(&q, 0, &a);          /* same request: still with the worker */
        CHECK(q.state == RS_RUNNING && renderq_wants(&q, 7));
        CHECK(!renderq_finish(&q, 6, RO_OK, 0));   /* someone else's answer */
        CHECK(q.state == RS_RUNNING);
        renderq_update(&q, 0, &b);          /* the view moved on */
        CHECK(q.state == RS_PENDING && !renderq_wants(&q, 7));
        CHECK(!renderq_finish(&q, 7, RO_OK, 0));   /* stale: never drawn */
        CHECK(q.state == RS_PENDING);
        renderq_start(&q, 8);
        CHECK(!renderq_finish(&q, 8, RO_ABORTED, 0));
        CHECK(q.state == RS_PENDING && q.attempts == 0);   /* an abort costs no attempt */
        renderq_start(&q, 9);
        CHECK(!renderq_finish(&q, 9, RO_FAILED, 0));
        CHECK(q.state == RS_RETRY && q.attempts == 1);
        renderq_update(&q, 1, &b);          /* covered meanwhile */
        CHECK(q.state == RS_READY);
        renderq_update(&q, 0, &a);
        renderq_start(&q, 10);
        CHECK(renderq_finish(&q, 10, RO_OK, 0));
        CHECK(q.state == RS_READY);
    }

    /* A result nobody takes is freed by stop; stop during a long job
     * returns promptly; the worker closes its document. */
    j = job(++id, 4, 0, 1);
    CHECK(rw_submit(&w, &j));
    sleep_ms(50);
    rw_stop(&w);
    CHECK(fini_called == 1);
    CHECK(live_buffers == 0);

    CHECK(rw_start(&w, &cbs, 0));
    rw_set_document(&w, 1, "a.pdf");
    j = job(1, 1, 0, 10000);
    CHECK(rw_submit(&w, &j));
    sleep_ms(30);
    t0 = now_ms();
    rw_stop(&w);
    CHECK(now_ms() - t0 < 500);
    CHECK(fini_called == 2);
    CHECK(live_buffers == 0);
    /* Stopping twice, or a worker that never started, is harmless. */
    rw_stop(&w);

    if (failures)
    {
        fprintf(stderr, "worker_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("worker_test: all checks passed\n");
    return 0;
}
