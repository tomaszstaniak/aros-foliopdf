/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef FOLIO_GPUPOLICY_H
#define FOLIO_GPUPOLICY_H
#include <stddef.h>
#include <stdint.h>
static int folio_gpu_has(const char *s, const char *needle) {
 for (; *s; ++s) { const char *a=s,*b=needle;
  while (*a && *b && ((*a>='A'&&*a<='Z'?*a+32:*a)==*b)) {++a;++b;}
  if (!*b) return 1;
 } return 0;
}
static int folio_gpu_renderer(const char *s) {
 if (!s || !*s) return 0;
 if (folio_gpu_has(s,"softpipe") || folio_gpu_has(s,"llvmpipe") ||
     folio_gpu_has(s,"swrast") || folio_gpu_has(s,"lavapipe") ||
     folio_gpu_has(s,"software") || folio_gpu_has(s,"unknown")) return 0;
 return folio_gpu_has(s,"radeon") || folio_gpu_has(s,"amd") ||
        folio_gpu_has(s,"intel") || folio_gpu_has(s,"nouveau") ||
        folio_gpu_has(s,"nvidia") || folio_gpu_has(s,"virgl") ||
        folio_gpu_has(s,"vc4") || folio_gpu_has(s,"v3d") ||
        folio_gpu_has(s,"panfrost") || folio_gpu_has(s,"freedreno");
}
static int folio_gpu_rect_fits(int ww,int wh,int x,int y,int w,int h) {
 return ww>0 && wh>0 && x>=0 && y>=0 && w>0 && h>0 &&
        x<=ww && y<=wh && w<=ww-x && h<=wh-y;
}
static size_t folio_gpu_image_bytes(int w,int h,int stride) {
 if(w<=0 || h<=0 || stride<=0 || (size_t)w>SIZE_MAX/3 ||
    (size_t)stride<(size_t)w*3 || (size_t)h>SIZE_MAX/(size_t)stride) return 0;
 return (size_t)h*(size_t)stride;
}
#endif
