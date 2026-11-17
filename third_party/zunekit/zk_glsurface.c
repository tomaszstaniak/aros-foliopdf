/* SPDX-License-Identifier: MIT */
/* zk_glsurface: see zk_glsurface.h. The same choice and the same texture
 * path as zk_glarea, without the Zune class. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <GL/gla.h>
#include "zk_glsurface.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_UNPACK_SKIP_ROWS
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

struct zk_glsurface {
    GLAContext ctx;
    GLuint tex;
    LONG width, height;             /* of the rectangle */
    LONG tw, th;                    /* size the texture was made for */
};

static char last_reason[96] = "not opened yet";
static char last_info[200];
static LONG last_software = -1;

const char *zk_glsurface_last_reason(void) { return last_reason; }
const char *zk_glsurface_last_info(void) { return last_info; }
LONG zk_glsurface_last_software(void) { return last_software; }

static int gs_software(const char *renderer)
{
    static const char *const names[] = { "softpipe", "llvmpipe", "swrast", "lavapipe", "Software", NULL };
    int i;

    for (i = 0; names[i]; i++)
        if (strstr(renderer, names[i]))
            return 1;
    return 0;
}

struct zk_glsurface *zk_glsurface_open(struct Window *window, LONG left, LONG top,
                                       LONG width, LONG height, LONG request)
{
    struct TagItem tags[] = {
        { GLA_Window, (IPTR)window },
        { GLA_Left, left }, { GLA_Top, top },
        { GLA_Width, width }, { GLA_Height, height },
        { GLA_DoubleBuf, GL_TRUE }, { GLA_RGBMode, GL_TRUE },
        { GLA_NoStencil, GL_TRUE }, { GLA_NoAccum, GL_TRUE },
        { TAG_DONE, 0 } };
    const char *vendor, *renderer, *version;
    struct zk_glsurface *s;

    last_info[0] = 0;
    last_software = -1;
    if (request == ZK_GLSURFACE_CPU) {
        strcpy(last_reason, "requested");
        return NULL;
    }
    if (!window || width <= 0 || height <= 0 || !(s = calloc(1, sizeof(*s)))) {
        strcpy(last_reason, "no OpenGL context");
        return NULL;
    }
    s->ctx = glACreateContext(tags);
    if (!s->ctx) {
        free(s);
        strcpy(last_reason, "no OpenGL context");
        return NULL;
    }
    glAMakeCurrent(s->ctx);
    vendor = (const char *)glGetString(GL_VENDOR);
    renderer = (const char *)glGetString(GL_RENDERER);
    version = (const char *)glGetString(GL_VERSION);
    snprintf(last_info, sizeof(last_info), "%s | %s | %s", vendor ? vendor : "?",
             renderer ? renderer : "?", version ? version : "?");
    last_software = gs_software(renderer ? renderer : "");
    if (request == ZK_GLSURFACE_AUTO && last_software) {
        strcpy(last_reason, "OpenGL here is a software rasteriser");
        glAMakeCurrent(NULL);
        glADestroyContext(s->ctx);
        free(s);
        return NULL;
    }
    strcpy(last_reason, request == ZK_GLSURFACE_GPU ? "requested" : "hardware OpenGL found");
    s->width = width;
    s->height = height;
    return s;
}

void zk_glsurface_close(struct zk_glsurface *s)
{
    if (!s)
        return;
    glAMakeCurrent(s->ctx);
    if (s->tex)
        glDeleteTextures(1, &s->tex);
    glAMakeCurrent(NULL);
    glADestroyContext(s->ctx);
    free(s);
}

void zk_glsurface_present(struct zk_glsurface *s, const UBYTE *pixels, LONG w, LONG h,
                          LONG stride, LONG format)
{
    zk_glsurface_present_rect(s, pixels, w, h, stride, format, 0, 0, w, h);
}

void zk_glsurface_present_rect(struct zk_glsurface *s, const UBYTE *pixels, LONG w, LONG h,
                               LONG stride, LONG format, LONG x, LONG y, LONG rw, LONG rh)
{
    GLenum glformat = format == ZK_GLSURFACE_BGRA ? GL_BGRA : GL_RGBA;
    float x1, y0;

    if (!s || !pixels || w <= 0 || h <= 0)
        return;
    glAMakeCurrent(s->ctx);
    glViewport(0, 0, s->width, s->height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
    if (!s->tex) {
        glGenTextures(1, &s->tex);
        glBindTexture(GL_TEXTURE_2D, s->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else
        glBindTexture(GL_TEXTURE_2D, s->tex);
    if (s->tw != w || s->th != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, glformat, GL_UNSIGNED_BYTE, pixels);
        s->tw = w;
        s->th = h;
    } else {
        /* Clip the changed rectangle to the picture, then upload only it. */
        if (x < 0) { rw += x; x = 0; }
        if (y < 0) { rh += y; y = 0; }
        if (rw > w - x) rw = w - x;
        if (rh > h - y) rh = h - y;
        if (rw > 0 && rh > 0) {
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, x);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, y);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, rw, rh, glformat, GL_UNSIGNED_BYTE, pixels);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    x1 = -1.0f + 2.0f * (float)w / (float)s->width;
    y0 = 1.0f - 2.0f * (float)h / (float)s->height;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(-1.0f, y0);
    glTexCoord2f(1, 1); glVertex2f(x1, y0);
    glTexCoord2f(1, 0); glVertex2f(x1, 1.0f);
    glTexCoord2f(0, 0); glVertex2f(-1.0f, 1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glFinish();
    glASwapBuffers(s->ctx);
}

int zk_glsurface_begin(struct zk_glsurface *s)
{
    int n;
    if (!s) return 0;
    glAMakeCurrent(s->ctx);
    for (n=0; n<32 && glGetError()!=GL_NO_ERROR; ++n) {}
    glViewport(0,0,s->width,s->height);
    return glGetError()==GL_NO_ERROR;
}
int zk_glsurface_end(struct zk_glsurface *s)
{
    if (!s || glGetError()!=GL_NO_ERROR) return 0;
    glASwapBuffers(s->ctx);
    return glGetError()==GL_NO_ERROR;
}
