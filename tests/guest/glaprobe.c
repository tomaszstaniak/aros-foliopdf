/*
 * glaprobe - does a gl.library (GLA) context work inside a Zune layout?
 *
 * Copyright (C) 2026 Tomasz Staniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A window laid out like Folio's: a panel on the left (a Register with a
 * list), a Balance divider and, where Folio's page strip is, an Area whose
 * MUIM_Draw renders a textured quad through a GLA context bound to the
 * window at the area's rectangle. The texture is a generated RGB "page".
 *
 *   glaprobe LOGFILE
 *
 * Keys in the window: p toggles the panel (the area is relaid out and the
 * context follows), b runs the benchmark, q quits. Lines in LOGFILE say
 * what the GL strings are, whether the texture and context were made, how
 * long one textured frame plus glASwapBuffers takes at a few scales, and
 * how long the CPU paths Folio uses take for the same image and area:
 * WritePixelArray of the visible rows, ScalePixelArray to the area, and
 * Folio's composed frame (AllocBitMap, WritePixelArray into it, blit).
 *
 * gl.library is opened by hand (GLBase is defined here, so libGL's
 * autoinit object is not linked and nothing opens it at start), only when
 * the window is about to draw. With gl.library missing the window still
 * opens and the CPU numbers are still measured.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <cybergraphx/cybergraphics.h>
#include <devices/rawkeycodes.h>
#include <clib/alib_protos.h>
#include <GL/gla.h>
#include <GL/gl.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

__attribute__((used)) unsigned long __stack = 1024 * 1024;

struct Library *GLBase;     /* ours: no libGL autoinit, see the top comment */

#define IMG_W 1200
#define IMG_H 1600
#define FRAMES 12

static FILE *logf;
static unsigned char *image;   /* IMG_W x IMG_H RGB */
static Object *app, *win, *side, *balance, *area;
static struct MUI_CustomClass *AreaClass;
static int gl_wanted = 1;      /* 0: never open gl.library (CPU only) */

static LONG now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (LONG)(tv.tv_sec * 1000000 + tv.tv_usec);
}

static void lg(const char *fmt, ...)
{
    va_list ap;
    if (!logf) return;
    fprintf(logf, "%8ld ", (long)(now_us() / 1000));
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
}

/* A page-like picture: white, grey rules, coloured blocks and a diagonal,
 * so a wrong stride, orientation or channel order is visible at once. */
static void make_image(void)
{
    LONG x, y;
    image = malloc((size_t)IMG_W * IMG_H * 3);
    if (!image) return;
    for (y = 0; y < IMG_H; y++)
    {
        unsigned char *row = image + (size_t)y * IMG_W * 3;
        for (x = 0; x < IMG_W; x++)
        {
            unsigned char r = 255, g = 255, b = 255;
            if (y % 40 < 2) r = g = b = 180;                       /* rules */
            if (x < 60) { r = 220; g = 40; b = 40; }               /* red left margin */
            if (y < 60) { r = 40; g = 160; b = 40; }               /* green top margin */
            if (x > IMG_W - 60) { r = 40; g = 40; b = 220; }       /* blue right margin */
            if (x > 200 && x < 600 && y > 300 && y < 500) { r = 30; g = 30; b = 30; }  /* a black block */
            if (abs((int)(x * IMG_H / IMG_W) - (int)y) < 3) { r = 255; g = 128; b = 0; } /* orange diagonal */
            row[x * 3] = r; row[x * 3 + 1] = g; row[x * 3 + 2] = b;
        }
    }
}

static int is_software_renderer(const char *s)
{
    static const char *marks[] = { "softpipe", "llvmpipe", "lavapipe", "swrast", "software", NULL };
    int i;
    if (!s) return -1;
    for (i = 0; marks[i]; i++)
        if (strcasestr(s, marks[i]))
            return 1;
    return 0;
}

/* --- the GL area ----------------------------------------------------------- */

struct AreaData
{
    GLAContext ctx;
    GLuint tex;
    LONG cw, ch;            /* size the context was made for */
    int gl_ok;              /* context and texture ready */
    int frames;             /* draws so far */
    float scale;            /* quad scale, for the benchmark */
    LONG off_x, off_y;      /* quad offset in pixels */
};

static int open_gl(void)
{
    BPTR home, cur;
    if (GLBase) return 1;
    if (!gl_wanted) return 0;
    /* As egui-aros does: gl.library's helper process inherits the opener's
     * directories, which would keep this program's drawer in use. */
    home = SetProgramDir(BNULL);
    cur = CurrentDir(BNULL);
    GLBase = OpenLibrary((CONST_STRPTR)"gl.library", 20);
    CurrentDir(cur);
    SetProgramDir(home);
    lg("gl.library %s", GLBase ? "opened" : "NOT available");
    return GLBase != NULL;
}

static void close_gl(void)
{
    if (GLBase) { CloseLibrary(GLBase); GLBase = NULL; lg("gl.library closed"); }
}

static void gl_destroy(struct AreaData *d)
{
    if (!d->ctx) return;
    glAMakeCurrent(d->ctx);
    if (d->tex) { glDeleteTextures(1, &d->tex); d->tex = 0; }
    glADestroyContext(d->ctx);
    d->ctx = NULL;
    d->gl_ok = 0;
    lg("context destroyed");
}

static int gl_create(Object *obj, struct AreaData *d)
{
    struct Window *w = _window(obj);
    LONG t0, t1;
    GLint maxtex = 0;
    const char *vendor, *renderer, *version;
    struct TagItem tags[] = {
        { GLA_Window, (IPTR)w },
        { GLA_Left,   _mleft(obj) - w->LeftEdge - 0 },
        { GLA_Top,    _mtop(obj) - w->TopEdge - 0 },
        { GLA_Width,  _mwidth(obj) },
        { GLA_Height, _mheight(obj) },
        { GLA_NoDepth, GL_TRUE },
        { GLA_NoStencil, GL_TRUE },
        { GLA_NoAccum, GL_TRUE },
        { TAG_DONE, 0 } };

    if (!open_gl()) return 0;
    /* GLA_Left/Top are offsets inside the window's rastport (layer
     * coordinates): the area's position in the window. */
    tags[1].ti_Data = _mleft(obj);
    tags[2].ti_Data = _mtop(obj);
    t0 = now_us();
    d->ctx = glACreateContext(tags);
    t1 = now_us();
    if (!d->ctx) { lg("glACreateContext FAILED for %ldx%ld at %ld,%ld", (long)_mwidth(obj), (long)_mheight(obj), (long)_mleft(obj), (long)_mtop(obj)); return 0; }
    glAMakeCurrent(d->ctx);
    vendor = (const char *)glGetString(GL_VENDOR);
    renderer = (const char *)glGetString(GL_RENDERER);
    version = (const char *)glGetString(GL_VERSION);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtex);
    lg("context %ldx%ld at %ld,%ld in %ld us; GL_VENDOR \"%s\" GL_RENDERER \"%s\" GL_VERSION \"%s\" software %d max-texture %d",
       (long)_mwidth(obj), (long)_mheight(obj), (long)_mleft(obj), (long)_mtop(obj), (long)(t1 - t0),
       vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?", is_software_renderer(renderer), (int)maxtex);
    d->cw = _mwidth(obj); d->ch = _mheight(obj);

    /* The texture: the whole image, RGB, rows top to bottom as MuPDF
     * makes them (so the quad maps t=0 to the top). */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, IMG_W);
    glGenTextures(1, &d->tex);
    glBindTexture(GL_TEXTURE_2D, d->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t0 = now_us();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, IMG_W, IMG_H, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
    glFinish();
    t1 = now_us();
    lg("glTexImage2D %dx%d RGB in %ld us, GL error %u", IMG_W, IMG_H, (long)(t1 - t0), (unsigned)glGetError());
    d->gl_ok = 1;
    return 1;
}

/* One frame: clear, the textured quad at scale/offset, swap. */
static LONG gl_frame(Object *obj, struct AreaData *d)
{
    LONG w = _mwidth(obj), h = _mheight(obj), t0 = now_us();
    float qw = IMG_W * d->scale, qh = IMG_H * d->scale;
    float x0 = d->off_x, y0 = d->off_y;
    glAMakeCurrent(d->ctx);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);        /* pixel coordinates, y down, as the window */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.6f, 0.6f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, d->tex);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x0, y0);
    glTexCoord2f(1, 0); glVertex2f(x0 + qw, y0);
    glTexCoord2f(1, 1); glVertex2f(x0 + qw, y0 + qh);
    glTexCoord2f(0, 1); glVertex2f(x0, y0 + qh);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glASwapBuffers(d->ctx);
    d->frames++;
    return now_us() - t0;
}

/* The CPU paths, on the real window rastport, timed. */
static LONG cpu_write(Object *obj)
{
    LONG w = _mwidth(obj), h = _mheight(obj), t0 = now_us();
    LONG cw = w < IMG_W ? w : IMG_W, ch = h < IMG_H ? h : IMG_H;
    WritePixelArray(image, 0, 0, IMG_W * 3, _rp(obj), _mleft(obj), _mtop(obj), cw, ch, RECTFMT_RGB);
    return now_us() - t0;
}

static LONG cpu_scale(Object *obj, float scale)
{
    LONG w = _mwidth(obj), h = _mheight(obj), t0 = now_us();
    LONG dw = (LONG)(IMG_W * scale), dh = (LONG)(IMG_H * scale);
    LONG cw = dw < w ? dw : w, ch = dh < h ? dh : h;
    /* ScalePixelArray scales the whole source into dw x dh; Folio clips the
     * destination by the layer. Here the layer clips to the window too. */
    ScalePixelArray(image, IMG_W, IMG_H, IMG_W * 3, _rp(obj), _mleft(obj), _mtop(obj), dw, dh, RECTFMT_RGB);
    (void)cw; (void)ch;
    return now_us() - t0;
}

static LONG cpu_compose(Object *obj)
{
    LONG w = _mwidth(obj), h = _mheight(obj), t0 = now_us();
    struct RastPort *rp = _rp(obj);
    struct BitMap *bm = AllocBitMap(w, h, GetBitMapAttr(rp->BitMap, BMA_DEPTH), BMF_MINPLANES, rp->BitMap);
    struct RastPort buf;
    LONG cw = w < IMG_W ? w : IMG_W, ch = h < IMG_H ? h : IMG_H;
    if (!bm) return -1;
    InitRastPort(&buf);
    buf.BitMap = bm;
    SetAPen(&buf, 0);
    RectFill(&buf, 0, 0, w - 1, h - 1);
    WritePixelArray(image, 0, 0, IMG_W * 3, &buf, 0, 0, cw, ch, RECTFMT_RGB);
    BltBitMapRastPort(bm, 0, 0, rp, _mleft(obj), _mtop(obj), w, h, 0xC0);
    FreeBitMap(bm);
    return now_us() - t0;
}

static int cmp_long(const void *a, const void *b)
{
    LONG x = *(const LONG *)a, y = *(const LONG *)b;
    return x < y ? -1 : x > y;
}

static void report(const char *what, LONG *t, int n)
{
    LONG lo, hi, med;
    qsort(t, n, sizeof(*t), cmp_long);
    lo = t[0]; hi = t[n - 1]; med = t[n / 2];
    lg("%s: %d frames, median %ld us, min %ld us, max %ld us", what, n, (long)med, (long)lo, (long)hi);
}

#define MUIM_Area_Bench 0x80450001UL
#define MUIM_Area_Toggle 0x80450002UL

static IPTR Area_Bench(struct IClass *cl, Object *obj)
{
    struct AreaData *d = INST_DATA(cl, obj);
    LONG t[FRAMES];
    int i;
    static const float scales[] = { 0.5f, 0.75f, 1.0f, 1.5f };
    unsigned s;
    lg("benchmark: area %ldx%ld", (long)_mwidth(obj), (long)_mheight(obj));
    if (d->gl_ok)
    {
        for (s = 0; s < sizeof(scales) / sizeof(scales[0]); s++)
        {
            char name[64];
            d->scale = scales[s];
            for (i = 0; i < FRAMES; i++)
            {
                d->off_x = -(i * 7) % 200; d->off_y = -(i * 13) % 400;
                t[i] = gl_frame(obj, d);
            }
            snprintf(name, sizeof(name), "GL textured quad + swap at scale %.2f", scales[s]);
            report(name, t, FRAMES);
        }
        /* A new render: the whole texture replaced, then one frame. */
        for (i = 0; i < FRAMES; i++)
        {
            LONG t0 = now_us();
            glAMakeCurrent(d->ctx);
            glBindTexture(GL_TEXTURE_2D, d->tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, IMG_W, IMG_H, GL_RGB, GL_UNSIGNED_BYTE, image);
            d->scale = 1.0f; d->off_x = 0; d->off_y = -(i * 13) % 400;
            gl_frame(obj, d);
            t[i] = now_us() - t0;
        }
        report("GL glTexSubImage2D whole image + frame", t, FRAMES);
        d->scale = 1.0f; d->off_x = d->off_y = 0;
        gl_frame(obj, d);
    }
    else
        lg("no GL: benchmark covers the CPU paths only");
    for (i = 0; i < FRAMES; i++) t[i] = cpu_write(obj);
    report("CPU WritePixelArray visible rows", t, FRAMES);
    for (i = 0; i < FRAMES; i++) t[i] = cpu_compose(obj);
    report("CPU compose (AllocBitMap + WritePixelArray + BltBitMapRastPort)", t, FRAMES);
    for (s = 0; s < sizeof(scales) / sizeof(scales[0]); s++)
    {
        char name[64];
        for (i = 0; i < FRAMES; i++) t[i] = cpu_scale(obj, scales[s]);
        snprintf(name, sizeof(name), "CPU ScalePixelArray at scale %.2f", scales[s]);
        report(name, t, FRAMES);
    }
    lg("benchmark done");
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
}

static IPTR Area_AskMinMax(struct IClass *cl, Object *obj, struct MUIP_AskMinMax *msg)
{
    IPTR ret = DoSuperMethodA(cl, obj, (Msg)msg);
    msg->MinMaxInfo->MinWidth += 160; msg->MinMaxInfo->MinHeight += 120;
    msg->MinMaxInfo->DefWidth += 600; msg->MinMaxInfo->DefHeight += 560;
    msg->MinMaxInfo->MaxWidth = MUI_MAXMAX; msg->MinMaxInfo->MaxHeight = MUI_MAXMAX;
    return ret;
}

static IPTR Area_Show(struct IClass *cl, Object *obj, Msg msg)
{
    IPTR ret = DoSuperMethodA(cl, obj, msg);
    lg("MUIM_Show: area %ldx%ld at %ld,%ld (window %ldx%ld)", (long)_mwidth(obj), (long)_mheight(obj),
       (long)_mleft(obj), (long)_mtop(obj), (long)_window(obj)->Width, (long)_window(obj)->Height);
    return ret;
}

static IPTR Area_Hide(struct IClass *cl, Object *obj, Msg msg)
{
    struct AreaData *d = INST_DATA(cl, obj);
    /* The layout is about to change: the context is bound to a rectangle
     * of the window, so it is made again at the next draw. */
    lg("MUIM_Hide");
    gl_destroy(d);
    return DoSuperMethodA(cl, obj, msg);
}

static IPTR Area_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg)
{
    struct AreaData *d = INST_DATA(cl, obj);
    LONG t;
    DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
        return 0;
    if (d->ctx && (d->cw != _mwidth(obj) || d->ch != _mheight(obj)))
        gl_destroy(d);
    if (!d->ctx && !gl_create(obj, d))
    {
        /* No GL: the CPU picture, so the window is not blank. */
        t = cpu_compose(obj);
        lg("draw (CPU compose) %ld us", (long)t);
        return 0;
    }
    d->scale = d->scale > 0 ? d->scale : 1.0f;
    t = gl_frame(obj, d);
    lg("draw (GL frame %d) %ld us, flags 0x%lx", d->frames, (long)t, (unsigned long)msg->flags);
    return 0;
}

static IPTR Area_Cleanup(struct IClass *cl, Object *obj, Msg msg)
{
    struct AreaData *d = INST_DATA(cl, obj);
    gl_destroy(d);
    return DoSuperMethodA(cl, obj, msg);
}

BOOPSI_DISPATCHER(IPTR, AreaDispatcher, cl, obj, msg)
{
    switch (msg->MethodID)
    {
        case MUIM_AskMinMax: return Area_AskMinMax(cl, obj, (struct MUIP_AskMinMax *)msg);
        case MUIM_Show:      return Area_Show(cl, obj, msg);
        case MUIM_Hide:      return Area_Hide(cl, obj, msg);
        case MUIM_Draw:      return Area_Draw(cl, obj, (struct MUIP_Draw *)msg);
        case MUIM_Cleanup:   return Area_Cleanup(cl, obj, msg);
        case MUIM_Area_Bench: return Area_Bench(cl, obj);
        default:             return DoSuperMethodA(cl, obj, msg);
    }
}
BOOPSI_DISPATCHER_END

static const char *titles[] = { "Pages", "Outline", NULL };

int main(int argc, char **argv)
{
    Object *list, *bench_btn, *panel_btn, *quit_btn;
    static const char *rows[] = { "one", "two", "three", "four", "five", NULL };
    int i, shown = 1;

    if (argc < 2) { fprintf(stderr, "usage: glaprobe LOGFILE [CPU]\n"); return 20; }
    logf = fopen(argv[1], "w");
    if (argc > 2 && !strcasecmp(argv[2], "CPU")) gl_wanted = 0;
    lg("glaprobe start, gl %s", gl_wanted ? "wanted" : "not wanted (CPU)");
    make_image();
    if (!image) { lg("no memory for the image"); return 20; }

    AreaClass = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(struct AreaData), AreaDispatcher);
    if (!AreaClass) return 20;

    app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"glaprobe",
        MUIA_Application_Base, (IPTR)"GLAPROBE",
        SubWindow, (win = WindowObject,
            MUIA_Window_Title, (IPTR)"glaprobe: GLA inside a Zune layout",
            MUIA_Window_ID, MAKE_ID('G','L','A','P'),
            WindowContents, (VGroup,
                Child, (HGroup,
                    Child, (panel_btn = SimpleButton("Panel")),
                    Child, (bench_btn = SimpleButton("Benchmark")),
                    Child, (quit_btn = SimpleButton("Quit")),
                End),
                Child, (HGroup,
                    Child, (side = VGroup,
                        MUIA_HorizWeight, 0,
                        Child, (RegisterObject,
                            MUIA_Register_Titles, (IPTR)titles,
                            Child, (ListviewObject,
                                MUIA_Listview_List, (list = ListObject,
                                    MUIA_Frame, MUIV_Frame_InputList,
                                    MUIA_List_SourceArray, (IPTR)rows,
                                End),
                            End),
                            Child, (TextObject, MUIA_Text_Contents, (IPTR)"outline", End),
                        End),
                        Child, (RectangleObject, MUIA_FixWidth, 170, MUIA_FixHeight, 1, End),
                    End),
                    Child, (balance = BalanceObject, End),
                    Child, (area = NewObject(AreaClass->mcc_Class, NULL,
                        MUIA_Frame, MUIV_Frame_Virtual,
                        MUIA_FillArea, FALSE,
                        TAG_DONE)),
                End),
            End),
        End),
    End;
    if (!app) { lg("no application"); return 20; }

    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(quit_btn, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(bench_btn, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, 1);
    DoMethod(panel_btn, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, 2);
    SET(win, MUIA_Window_Open, TRUE);
    for (;;)
    {
        ULONG sigs = 0;
        IPTR id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (id == (IPTR)MUIV_Application_ReturnID_Quit) break;
        if (id == 1) DoMethod(area, MUIM_Area_Bench);
        if (id == 2)
        {
            shown = !shown;
            lg("panel %s", shown ? "shown" : "hidden");
            SET(side, MUIA_ShowMe, shown);
            SET(balance, MUIA_ShowMe, shown);
        }
        if (sigs)
        {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C) break;
        }
    }
    SET(win, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(app);
    MUI_DeleteCustomClass(AreaClass);
    close_gl();
    for (i = 0; i < 1; i++) ;
    lg("glaprobe end");
    fclose(logf);
    free(image);
    return 0;
}
