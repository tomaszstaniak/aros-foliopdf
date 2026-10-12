/* Render worker: one thread that rasterises pages away from the UI task.
 *
 * The UI task owns everything Zune and Intuition; the worker owns its own
 * MuPDF context and its own copy of the open document, made through the
 * callbacks below. Nothing MuPDF crosses between them. What does cross is
 * this structure, always under `lock`:
 *
 *   UI -> worker  the document to have open (path + generation), one job
 *                 at a time, an abort request, and quit;
 *   worker -> UI  one result: a malloc'd RGB buffer that becomes the UI
 *                 task's property when it takes the result.
 *
 * One job and one result at most: the UI picks the next job only when the
 * previous one has been answered, so the most useful page is chosen with
 * the view as it is then, and nothing stale piles up in a queue.
 *
 * No MuPDF, MUI or AROS dependencies: tests/worker_test.c drives this with
 * fake callbacks on the host. */

#ifndef FOLIO_RENDERWORKER_H
#define FOLIO_RENDERWORKER_H

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define RW_PATH_MAX 512
#define RW_MSG_MAX  96

struct RwJob
{
    unsigned int id;        /* never 0 */
    unsigned int doc_gen;   /* the document the request was made for */
    unsigned int page_gen;  /* the page content it was made for (highlights) */
    int kind, page;
    LONG w, h, bandY, bandH;
};

struct RwResult
{
    struct RwJob job;
    int outcome;            /* RO_* from renderq.h */
    unsigned char *rgb;     /* RO_OK: rows of `stride` bytes; the taker frees it */
    LONG w, h, stride;
    LONG ms;                /* worker time for the job */
    char msg[RW_MSG_MAX];   /* RO_FAILED: why */
};

/* Run in the worker thread. `wctx` is what init returned. */
struct RwCallbacks
{
    void *(*init)(void *user);                  /* NULL: the worker cannot run */
    void (*thread_start)(void *wctx);           /* first thing in the thread; may be NULL */
    void (*fini)(void *wctx);
    int (*open)(void *wctx, const char *path, char *msg, size_t msgsize);   /* 1 = open */
    void (*close)(void *wctx);
    /* Start a job: called with the lock held, so an abort cannot slip in
     * between taking the job and clearing the previous job's abort flag. */
    void (*prepare)(void *wctx);
    /* Called from the UI task, with the lock held, for the running job. */
    void (*abort)(void *wctx);
    /* Render; fill outcome, rgb, w, h, stride, msg. */
    void (*render)(void *wctx, const struct RwJob *job, struct RwResult *res);
    /* A result is waiting. Called without the lock; must only signal. */
    void (*notify)(void *user);
    void *user;
};

struct RenderWorker
{
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_t thread;
    int started;
    struct RwCallbacks cb;
    void *wctx;

    int quit;
    unsigned int want_gen;          /* document the UI wants open */
    char want_path[RW_PATH_MAX];
    unsigned int have_gen;          /* document the worker has open (or failed to) */
    int doc_ok;
    char doc_msg[RW_MSG_MAX];

    int has_job;                    /* submitted, not yet taken */
    struct RwJob job;
    int busy;                       /* job `running` is being rendered */
    struct RwJob running;
    int abort_sent;
    int has_result;
    struct RwResult result;

    /* Where the thread is, for diagnosis: 1 running, 2 opening a document,
     * 3 waiting, 4 rendering. Written by the worker, read without the lock. */
    volatile int stage;
    volatile int exited;            /* AROS: the thread has left rw_main */
    /* Counters for -t, read under the lock. */
    unsigned long n_done, n_failed, n_aborted;
    long busy_ms;
};

static void rw_take_job(struct RenderWorker *w, struct RwResult *res)
{
    memset(res, 0, sizeof(*res));
    res->job = w->job;
    w->running = w->job;
    w->has_job = 0;
    w->busy = 1;
    w->abort_sent = 0;
    if (w->cb.prepare)
        w->cb.prepare(w->wctx);
}

static void *rw_main(void *arg)
{
    struct RenderWorker *w = arg;
    struct RwResult res;

    if (w->cb.thread_start)
        w->cb.thread_start(w->wctx);
    w->stage = 1;
    pthread_mutex_lock(&w->lock);
    for (;;)
    {
        w->stage = 3;
        while (!w->quit && w->want_gen == w->have_gen && !(w->has_job && !w->has_result))
            pthread_cond_wait(&w->wake, &w->lock);
        if (w->quit)
            break;
        if (w->want_gen != w->have_gen)
        {
            /* A document change goes before any job: a job for the new
             * document needs it, and one for the old one is stale. */
            char path[RW_PATH_MAX], msg[RW_MSG_MAX];
            unsigned int gen = w->want_gen;
            int ok;
            memcpy(path, w->want_path, sizeof(path));
            pthread_mutex_unlock(&w->lock);
            w->stage = 2;
            w->cb.close(w->wctx);
            msg[0] = 0;
            ok = path[0] ? w->cb.open(w->wctx, path, msg, sizeof(msg)) : 0;
            pthread_mutex_lock(&w->lock);
            w->have_gen = gen;
            w->doc_ok = ok;
            memcpy(w->doc_msg, msg, sizeof(msg));
            continue;
        }
        rw_take_job(w, &res);
        if (res.job.doc_gen != w->have_gen || res.job.doc_gen != w->want_gen)
        {
            /* Made for another document: nothing to render. */
            res.outcome = RO_ABORTED;
        }
        else if (!w->doc_ok)
        {
            res.outcome = RO_FAILED;
            memcpy(res.msg, w->doc_msg, sizeof(res.msg));
        }
        else
        {
            pthread_mutex_unlock(&w->lock);
            w->stage = 4;
            w->cb.render(w->wctx, &res.job, &res);
            pthread_mutex_lock(&w->lock);
            /* An abort that came after the render had already finished
             * does not undo a good image; the UI decides whether it still
             * wants it. */
        }
        w->busy = 0;
        if (res.outcome == RO_OK) w->n_done++;
        else if (res.outcome == RO_FAILED) w->n_failed++;
        else w->n_aborted++;
        w->busy_ms += res.ms;
        w->result = res;
        w->has_result = 1;
        pthread_mutex_unlock(&w->lock);
        if (w->cb.notify)
            w->cb.notify(w->cb.user);
        pthread_mutex_lock(&w->lock);
    }
    pthread_mutex_unlock(&w->lock);
    w->cb.close(w->wctx);
    return NULL;
}

/* Starting and joining the thread.
 *
 * On AROS the worker is a DOS process made here rather than by
 * pthread_create(): the ABIv11 libpthread starts a thread by signalling
 * SIGF_SINGLE, which the new process's own start-up can consume, so the
 * thread may never start and a join would then wait for ever (fixed in
 * mainline libpthread, d62046e69d, 2026-09-29, not in ABIv11). The mutex
 * and condition variable stay pthread ones; they work from any task. The
 * process holds no lock on the program's drawer (NP_HomeDir and
 * NP_CurrentDir are given as none) and runs at priority -1, below the UI
 * task: Exec always runs the higher-priority task that is ready, so the
 * worker gets the CPU only while the UI waits for input. At equal priority
 * they shared it in time slices, which showed as 50-110 ms delays in the
 * UI on one emulated CPU (AROS One, 2026-10-12). */
#ifdef __AROS__
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>

static struct RenderWorker *rw_spawned;     /* one worker per program */

static void rw_entry(void)
{
    struct RenderWorker *w = rw_spawned;
    rw_main(w);
    /* Under Forbid until this process is gone: the UI task, which waits
     * for `exited`, cannot unload this code while it is still running. */
    Forbid();
    w->exited = 1;
}

static int rw_spawn(struct RenderWorker *w, size_t stack)
{
    rw_spawned = w;
    return CreateNewProcTags(NP_Entry, (IPTR)rw_entry,
                             NP_Name, (IPTR)"Folio render",
                             NP_StackSize, stack ? stack : 65536,
                             NP_Priority, -1,
                             NP_HomeDir, (IPTR)BNULL,
                             NP_CurrentDir, (IPTR)BNULL,
                             TAG_DONE) != NULL;
}

static void rw_join(struct RenderWorker *w)
{
    while (!w->exited)
        Delay(1);
}
#else
static int rw_spawn(struct RenderWorker *w, size_t stack)
{
    pthread_attr_t attr;
    int ok;
    pthread_attr_init(&attr);
    if (stack)
        pthread_attr_setstacksize(&attr, stack);
    ok = pthread_create(&w->thread, &attr, rw_main, w) == 0;
    pthread_attr_destroy(&attr);
    return ok;
}

static void rw_join(struct RenderWorker *w)
{
    pthread_join(w->thread, NULL);
}
#endif

/* Start the thread. Returns 1 on success; on 0 the caller renders itself. */
static int rw_start(struct RenderWorker *w, const struct RwCallbacks *cb, size_t stack)
{
    memset(w, 0, sizeof(*w));
    w->cb = *cb;
    if (pthread_mutex_init(&w->lock, NULL))
        return 0;
    if (pthread_cond_init(&w->wake, NULL))
    {
        pthread_mutex_destroy(&w->lock);
        return 0;
    }
    /* The context is made here rather than in the thread, so a failure is
     * known before the UI relies on the worker. MuPDF contexts are not
     * tied to the thread that made them; this one is only ever used by
     * the worker from now on. */
    w->wctx = cb->init(cb->user);
    if (!w->wctx)
        goto fail;
    /* MuPDF and FreeType need a large stack; a pthread's default copies
     * the main task's, which depends on how the program was started. */
    if (!rw_spawn(w, stack))
    {
        cb->fini(w->wctx);
        goto fail;
    }
    w->started = 1;
    return 1;
fail:
    pthread_cond_destroy(&w->wake);
    pthread_mutex_destroy(&w->lock);
    return 0;
}

/* Abort the running job if it is `id` (0: whatever runs). Lock held.
 * Returns 1 when the abort was sent now, 0 if not running or already sent:
 * a render that is decoding one large image only notices at the end. */
static int rw_abort_locked(struct RenderWorker *w, unsigned int id)
{
    if (w->busy && !w->abort_sent && (id == 0 || w->running.id == id))
    {
        w->abort_sent = 1;
        if (w->cb.abort)
            w->cb.abort(w->wctx);
        return 1;
    }
    return 0;
}

static int rw_abort(struct RenderWorker *w, unsigned int id)
{
    int sent;
    pthread_mutex_lock(&w->lock);
    sent = rw_abort_locked(w, id);
    pthread_mutex_unlock(&w->lock);
    return sent;
}

/* The UI now shows document `gen` from `path` ("" for none). The running
 * job, made for the previous one, is aborted, and a waiting one dropped. */
static void rw_set_document(struct RenderWorker *w, unsigned int gen, const char *path)
{
    pthread_mutex_lock(&w->lock);
    w->want_gen = gen;
    strncpy(w->want_path, path ? path : "", sizeof(w->want_path) - 1);
    w->want_path[sizeof(w->want_path) - 1] = 0;
    w->has_job = 0;
    rw_abort_locked(w, 0);
    pthread_cond_signal(&w->wake);
    pthread_mutex_unlock(&w->lock);
}

/* May a job be submitted now: nothing waiting, running or unanswered. */
static int rw_idle(struct RenderWorker *w)
{
    int idle;
    pthread_mutex_lock(&w->lock);
    idle = !w->has_job && !w->busy && !w->has_result;
    pthread_mutex_unlock(&w->lock);
    return idle;
}

/* What the worker holds now: 0 idle, else the job id it renders or will. */
static unsigned int rw_current(struct RenderWorker *w, int *kind)
{
    unsigned int id = 0;
    pthread_mutex_lock(&w->lock);
    if (w->busy) { id = w->running.id; if (kind) *kind = w->running.kind; }
    else if (w->has_job) { id = w->job.id; if (kind) *kind = w->job.kind; }
    pthread_mutex_unlock(&w->lock);
    return id;
}

static int rw_submit(struct RenderWorker *w, const struct RwJob *job)
{
    int ok;
    pthread_mutex_lock(&w->lock);
    ok = !w->has_job && !w->busy && !w->has_result && !w->quit;
    if (ok)
    {
        w->job = *job;
        w->has_job = 1;
        pthread_cond_signal(&w->wake);
    }
    pthread_mutex_unlock(&w->lock);
    return ok;
}

/* Take the result, if there is one. The buffer is the caller's to free. */
static int rw_take_result(struct RenderWorker *w, struct RwResult *res)
{
    int got;
    pthread_mutex_lock(&w->lock);
    got = w->has_result;
    if (got)
    {
        *res = w->result;
        w->has_result = 0;
        w->result.rgb = NULL;
        /* The worker may have a document change to make now. */
        pthread_cond_signal(&w->wake);
    }
    pthread_mutex_unlock(&w->lock);
    return got;
}

/* Stop the thread and wait for it. Whatever it was rendering is aborted;
 * a result nobody took is freed. Safe on a worker that never started. */
static void rw_stop(struct RenderWorker *w)
{
    if (!w->started)
        return;
    pthread_mutex_lock(&w->lock);
    w->quit = 1;
    w->has_job = 0;
    rw_abort_locked(w, 0);
    pthread_cond_signal(&w->wake);
    pthread_mutex_unlock(&w->lock);
    rw_join(w);
    w->cb.fini(w->wctx);
    free(w->result.rgb);
    w->result.rgb = NULL;
    w->has_result = 0;
    pthread_cond_destroy(&w->wake);
    pthread_mutex_destroy(&w->lock);
    w->started = 0;
}

#endif
