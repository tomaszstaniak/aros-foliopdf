/* SPDX-License-Identifier: MIT */
/* zk_glsurface: the part of zk_glarea a program can use without the class,
 * for an existing Zune (or Intuition) object that already owns a rectangle
 * of a window and composes its picture in memory: open OpenGL on that
 * rectangle if it is worth it, hand pictures to it, close it when the
 * rectangle goes away (hidden or resized).
 *
 *   s = zk_glsurface_open(window, left, top, width, height, ZK_GLSURFACE_AUTO);
 *   if (s) zk_glsurface_present(s, pixels, w, h, stride, ZK_GLSURFACE_BGRA);
 *   else   ... the program's own CPU path (WritePixelArray) ...
 *   zk_glsurface_close(s);
 *
 * zk_glsurface_open() returns NULL when the CPU should be used; the reason
 * and the OpenGL texts of the last call are kept and can be read with
 * zk_glsurface_last_reason() and zk_glsurface_last_info() (static texts,
 * one program-wide copy, main task only).
 *
 * OpenGL needs a large stack (8 MB in the tests). Link with -lGL. */
#ifndef ZK_GLSURFACE_H
#define ZK_GLSURFACE_H

#include <exec/types.h>
#include <intuition/intuition.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zk_glsurface_request {
    ZK_GLSURFACE_AUTO,        /* OpenGL unless it is a software rasteriser */
    ZK_GLSURFACE_CPU,         /* never OpenGL: open returns NULL */
    ZK_GLSURFACE_GPU          /* OpenGL whatever the renderer is */
};

enum zk_glsurface_format {
    ZK_GLSURFACE_RGBA,        /* bytes R, G, B, A */
    ZK_GLSURFACE_BGRA         /* bytes B, G, R, A (cairo ARGB32 on little endian) */
};

struct zk_glsurface;

struct zk_glsurface *zk_glsurface_open(struct Window *window, LONG left, LONG top,
                                       LONG width, LONG height, LONG request);
void zk_glsurface_close(struct zk_glsurface *s);
/* Local extension: UI task only. Begin makes the context current; end
 * swaps only a successful frame. Return 0 on a GL error. */
int zk_glsurface_begin(struct zk_glsurface *s);
int zk_glsurface_end(struct zk_glsurface *s);

/* Shows w x h pixels (rows top to bottom, stride bytes per row) with their
 * corner at the rectangle's corner, and swaps buffers. Alpha is ignored. */
void zk_glsurface_present(struct zk_glsurface *s, const UBYTE *pixels, LONG w, LONG h,
                          LONG stride, LONG format);

/* As zk_glsurface_present(), for a picture of which only the rectangle
 * x, y, rw, rh (picture pixels) changed since the last call with the same
 * size: only that part is uploaded. The whole picture is uploaded when the
 * size differs from the last call. */
void zk_glsurface_present_rect(struct zk_glsurface *s, const UBYTE *pixels, LONG w, LONG h,
                               LONG stride, LONG format, LONG x, LONG y, LONG rw, LONG rh);

/* "requested", "hardware OpenGL found", "OpenGL here is a software
 * rasteriser", "no OpenGL context". */
const char *zk_glsurface_last_reason(void);
/* "vendor | renderer | version", "" when no context was opened. */
const char *zk_glsurface_last_info(void);
/* 1 software rasteriser, 0 not, -1 no context was opened. */
LONG zk_glsurface_last_software(void);

#ifdef __cplusplus
}
#endif

#endif
