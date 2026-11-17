/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Included by folio.c: the UI task owns the context and cached textures. */
#include "gpupolicy.h"
#include "gputile.h"
static unsigned fg_fail_frame, fg_fail_upload, fg_frames, fg_uploads;
static int fg_request; /* 0 AUTO, 1 CPU, 2 diagnostic GL */
#ifdef FOLIO_OPENGL
struct Library *GLBase; /* Owned here: prevents mandatory GL autoinit. */
#include <GL/gla.h>
#include "zk_glsurface.h"
#define FG_SLOTS 128
/* A rejected AUTO probe is stable for the lifetime of this screen/bitmap.
 * Hide/show must not recreate a software context on every resize. */
static struct Screen *fg_cpu_screen;
static struct BitMap *fg_cpu_bitmap;
#define FG_BUDGET (64u*1024u*1024u)
struct fg_texture { const void *key; unsigned long generation; GLuint id; int w,h, tx,ty; size_t bytes; unsigned long used; };
static struct { struct zk_glsurface *surface; struct Window *window; LONG l,t,w,h,ww,wh;
 int attempted,failed,active,reconfigure; unsigned long clock; size_t bytes;
 struct fg_texture tex[FG_SLOTS]; } fg;
static void fg_drop(void) {
 int i;
 if(fg.surface) {
  zk_glsurface_begin(fg.surface);
  for(i=0;i<FG_SLOTS;i++) if(fg.tex[i].id) glDeleteTextures(1,&fg.tex[i].id);
  zk_glsurface_close(fg.surface);
 }
 if(GLBase) {CloseLibrary(GLBase);GLBase=NULL;}
 memset(&fg,0,sizeof(fg));
}
static void fg_forget(const void *key) {
 int i;
 if(!fg.surface || !key) return;
 zk_glsurface_begin(fg.surface);
 for(i=0;i<FG_SLOTS;i++) if(fg.tex[i].key==key) {
  glDeleteTextures(1,&fg.tex[i].id); fg.bytes-=fg.tex[i].bytes;
  memset(&fg.tex[i],0,sizeof(fg.tex[i]));
 }
}
static unsigned fg_pen(Object *obj,ULONG pen) {
 ULONG rgb[3];
 GetRGB32(_screen(obj)->ViewPort.ColorMap,pen,1,rgb);
 return (rgb[0]&0xff000000)>>8 | (rgb[1]&0xff000000)>>16 | (rgb[2]&0xff000000)>>24;
}
static void fg_rect(float x,float y,float w,float h,unsigned rgb,int invert) {
 if(!fg.active || w<=0 || h<=0) return;
 glDisable(GL_TEXTURE_2D);
 if(invert) {glEnable(GL_COLOR_LOGIC_OP);glLogicOp(GL_INVERT);}
 glColor3ub(rgb>>16,rgb>>8,rgb);
 glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
 if(invert) glDisable(GL_COLOR_LOGIC_OP);
}
static void fg_prepare(Object *obj) {
 GLint maxtex=0; const char *renderer; int stable_cpu=0;
 if(fg.surface && (fg.window!=_window(obj)||fg.l!=_mleft(obj)||fg.t!=_mtop(obj)||fg.w!=_mwidth(obj)||fg.h!=_mheight(obj))) fg_drop();
 if(fg_request==1) return;
 if(fg_request==0 && fg_cpu_screen==_screen(obj) &&
    fg_cpu_bitmap==_screen(obj)->RastPort.BitMap) return;
 if(!folio_gpu_rect_fits(_window(obj)->Width,_window(obj)->Height,
                         _mleft(obj),_mtop(obj),_mwidth(obj),_mheight(obj))) return;
 if(!fg.attempted) {
  fg.attempted=1; fg.window=_window(obj);fg.l=_mleft(obj);fg.t=_mtop(obj);fg.w=_mwidth(obj);fg.h=_mheight(obj);fg.ww=fg.window->Width;fg.wh=fg.window->Height;
  GLBase=OpenLibrary((CONST_STRPTR)"gl.library",20);
  if(GLBase) fg.surface=zk_glsurface_open(fg.window,fg.l,fg.t,fg.w,fg.h,fg_request==2?ZK_GLSURFACE_GPU:ZK_GLSURFACE_AUTO);
  if(fg.surface) {
   renderer=(const char*)glGetString(GL_RENDERER);glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxtex);
   if((fg_request!=2 && !folio_gpu_renderer(renderer))||maxtex<1024) {stable_cpu=1;zk_glsurface_close(fg.surface);fg.surface=NULL;}
  }
  if(fg.surface) {
   const char *version=(const char*)glGetString(GL_VERSION);
   const char *ext=(const char*)glGetString(GL_EXTENSIONS);
   if(!version || (atoi(version)<2 && (!ext || !strstr(ext,"GL_ARB_texture_non_power_of_two")))) {
    stable_cpu=1;zk_glsurface_close(fg.surface);fg.surface=NULL;
   }
  }
  if(!fg.surface && fg_request==0 && (stable_cpu || (GLBase && zk_glsurface_last_software()==1))) {
   fg_cpu_screen=_screen(obj);fg_cpu_bitmap=fg_cpu_screen->RastPort.BitMap;
  }
  if(!fg.surface && GLBase) {CloseLibrary(GLBase);GLBase=NULL;}
  if(log_file) log_event("renderer %s: %s; area %ld,%ld %ldx%ld window %ldx%ld",
    fg.surface?"GPU":"CPU",zk_glsurface_last_info(),(long)fg.l,(long)fg.t,(long)fg.w,(long)fg.h,(long)fg.ww,(long)fg.wh);
 }
}
static int fg_begin(Object *obj) {
 if(fg.surface && (fg.window!=_window(obj)||fg.l!=_mleft(obj)||fg.t!=_mtop(obj)||fg.w!=_mwidth(obj)||fg.h!=_mheight(obj)||fg.ww!=fg.window->Width||fg.wh!=fg.window->Height)) {
  if(!fg.reconfigure) {
   fg.reconfigure=1;
   DoMethod(app_obj,MUIM_Application_PushMethod,(IPTR)reader_obj,1,MUIM_Reader_GPUPrepare);
  }
  return 0;
 }
 if(!fg.surface||fg.failed) return 0;
 if((fg_fail_frame && ++fg_frames==fg_fail_frame) || !zk_glsurface_begin(fg.surface)) {
  fg_drop();fg.failed=fg.attempted=1;return 0;
 }
 fg.active=1;
 glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,fg.w,fg.h,0,-1,1);
 glMatrixMode(GL_MODELVIEW);glLoadIdentity();
 {
  unsigned bg=fg_pen(obj,_dri(obj)->dri_Pens[BACKGROUNDPEN]);
  glClearColor(((bg>>16)&255)/255.f,((bg>>8)&255)/255.f,(bg&255)/255.f,1);
 }
 glClear(GL_COLOR_BUFFER_BIT);
 return 1;
}
static void fg_tile(const void *key,unsigned long generation,const unsigned char *rgb,int w,int h,int stride,int tx,int ty,float x,float y,float dw,float dh,float u0,float v0,float u1,float v1) {
 int i,slot=-1;size_t bytes=folio_gpu_image_bytes(w,h,stride);GLint maxtex;struct fg_texture *e;
 if(!fg.active||fg.failed) return;
 glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxtex);
 if(!bytes || (size_t)w*(size_t)h>FG_BUDGET/4 || w>maxtex||h>maxtex) {fg.failed=1;return;}
 for(i=0;i<FG_SLOTS;i++) if(fg.tex[i].key==key && fg.tex[i].generation==generation && fg.tex[i].tx==tx && fg.tex[i].ty==ty) {slot=i;break;}
 if(slot<0) {
  size_t needed=(size_t)w*h*4;
  while(fg.bytes+needed>FG_BUDGET) {
   int oldest=-1;
   for(i=0;i<FG_SLOTS;i++) if(fg.tex[i].id&&(oldest<0||fg.tex[i].used<fg.tex[oldest].used)) oldest=i;
   if(oldest<0) {fg.failed=1;return;}
   glDeleteTextures(1,&fg.tex[oldest].id);fg.bytes-=fg.tex[oldest].bytes;memset(&fg.tex[oldest],0,sizeof(fg.tex[oldest]));
  }
  for(i=0;i<FG_SLOTS;i++) if(!fg.tex[i].id) {slot=i;break;}
  if(slot<0) {slot=0;for(i=1;i<FG_SLOTS;i++) if(fg.tex[i].used<fg.tex[slot].used) slot=i;
   glDeleteTextures(1,&fg.tex[slot].id);fg.bytes-=fg.tex[slot].bytes;}
  e=&fg.tex[slot];memset(e,0,sizeof(*e));
  if(fg_fail_upload && ++fg_uploads==fg_fail_upload) {fg.failed=1;return;}
  glGenTextures(1,&e->id);glBindTexture(GL_TEXTURE_2D,e->id);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812F);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  if(stride%3) {fg.failed=1;return;}
  glPixelStorei(0x0CF2,stride/3); /* GL_UNPACK_ROW_LENGTH */
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,rgb);
  glPixelStorei(0x0CF2,0);
  if(glGetError()!=GL_NO_ERROR) {glDeleteTextures(1,&e->id);memset(e,0,sizeof(*e));fg.failed=1;return;}
  e->key=key;e->generation=generation;e->tx=tx;e->ty=ty;e->bytes=needed;fg.bytes+=needed;
 }
 e=&fg.tex[slot];e->used=++fg.clock;
 glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,e->id);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
 glBegin(GL_QUADS);
 glTexCoord2f(u0,v0);glVertex2f(x,y);glTexCoord2f(u1,v0);glVertex2f(x+dw,y);
 glTexCoord2f(u1,v1);glVertex2f(x+dw,y+dh);glTexCoord2f(u0,v1);glVertex2f(x,y+dh);
 glEnd();glDisable(GL_TEXTURE_2D);
}
static void fg_image(const void *key,unsigned long generation,const unsigned char *rgb,int w,int h,int stride,float x,float y,float dw,float dh) {
 int tx,ty;
 if(!folio_gpu_image_bytes(w,h,stride)) {fg.failed=1;return;}
 for(ty=0;ty<h;ty+=512) for(tx=0;tx<w;tx+=512) {
  struct folio_tile t;
  if(!folio_tile_region(w,h,tx,ty,512,&t)) {fg.failed=1;return;}
  if(x+dw*(tx+t.inner_w)/w<=0 || y+dh*(ty+t.inner_h)/h<=0 ||
     x+dw*tx/w>=fg.w || y+dh*ty/h>=fg.h) continue;
  fg_tile(key,generation,rgb+(size_t)t.y*stride+(size_t)t.x*3,t.w,t.h,stride,tx,ty,
          x+dw*tx/w,y+dh*ty/h,dw*t.inner_w/w,dh*t.inner_h/h,t.u0,t.v0,t.u1,t.v1);
  if(fg.failed) return;
 }
}

#else
static void fg_drop(void) {}
static void fg_forget(const void *key) {(void)key;}
#endif
