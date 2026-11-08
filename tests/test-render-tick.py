#!/usr/bin/env python3
"""Exercise production timer/counting functions with the MUI boundary stubbed."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/folio.c').read_text()
def function(signature):
    start = source.index(signature + '\n{')
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

preamble = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
typedef int LONG;
typedef int IPTR;
#include "renderq.h"
#include "renderpolicy.h"
enum { KIND_THUMB, KIND_MAIN, KIND_COUNT };
struct CellData { struct RenderQ rq; int visible; } cells[2];
static struct CellData *pages = &cells[1];
static struct { void *cells; } cols[2] = {{&cells[0]}, {&cells[1]}};
static int page_count = 1, layout_valid = 1, worker_on, show_timing;
static int stopped, active_job;
static LONG strip_top, last_scroll_top, last_scroll_ms = 1000, clock_ms = 1200;
static void *log_file;
static struct { int stage; } worker;
static LONG now_ms(void) { return clock_ms; }
static struct CellData *cell_data(int kind, int i) { (void)i; return &cells[kind]; }
static void place_pages(void) {}
static void place_thumbs(void) {}
static void check_running(void) {}
static void log_event(const char *fmt, ...) { (void)fmt; }
static int schedule(void) { return active_job; }
static void stop_render_timer(void) { stopped = 1; }
static void update_label(void) {}
'''
main = r'''
int main(void) {
    cells[0].visible = 1;
    cells[0].rq.state = RS_PENDING;
    cells[1].rq.state = RS_READY;
    Reader_RenderTick();
    assert(!stopped); /* Deferred visible thumbnails need a future tick. */
    cells[0].rq.state = RS_RETRY;
    Reader_RenderTick();
    assert(!stopped);
    cells[0].rq.state = RS_PENDING;
    cells[0].visible = 0;
    Reader_RenderTick();
    assert(stopped); /* Hidden pending work must not keep polling forever. */
    stopped = 0;
    active_job = 1;
    Reader_RenderTick();
    assert(!stopped);
    active_job = 0;
    cells[0].visible = 1;
    cells[0].rq.state = RS_READY;
    Reader_RenderTick();
    assert(stopped);
    puts("render_tick_test: all checks passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path/'test.c').write_text(preamble + function('static int count_states(int *pending, int *retry, int *failed)') + function('static IPTR Reader_RenderTick(void)') + main)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Wno-unused-function','-I'+str(root/'src'),str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
