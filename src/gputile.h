/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef FOLIO_GPUTILE_H
#define FOLIO_GPUTILE_H
struct folio_tile {
 int x,y,w,h,inner_w,inner_h;
 float u0,v0,u1,v1;
};
/* Include the neighbouring texel on every internal edge. Linear filtering
 * then samples the original image across a tile boundary, avoiding seams. */
static int folio_tile_region(int w,int h,int x,int y,int edge,struct folio_tile *t)
{
 int right,bottom;
 if(w<=0 || h<=0 || x<0 || y<0 || x>=w || y>=h || edge<=0 || !t) return 0;
 t->inner_w=w-x<edge?w-x:edge; t->inner_h=h-y<edge?h-y:edge;
 t->x=x>0?x-1:0;t->y=y>0?y-1:0;
 right=x+t->inner_w;bottom=y+t->inner_h;
 if(right<w) ++right;
 if(bottom<h) ++bottom;
 t->w=right-t->x;t->h=bottom-t->y;
 t->u0=(float)(x-t->x)/t->w;t->v0=(float)(y-t->y)/t->h;
 t->u1=(float)(x+t->inner_w-t->x)/t->w;
 t->v1=(float)(y+t->inner_h-t->y)/t->h;
 return 1;
}
#endif
