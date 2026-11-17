#include <assert.h>
#include "../src/gputile.h"
int main(void) {
 struct folio_tile t;
 assert(folio_tile_region(1200,800,512,0,512,&t));
 assert(t.x==511 && t.w==514 && t.inner_w==512);
 assert(t.u0>0 && t.u1<1);
 assert(folio_tile_region(1200,800,1024,512,512,&t));
 assert(t.x==1023 && t.w==177 && t.inner_w==176);
 assert(t.y==511 && t.h==289 && t.inner_h==288);
 assert(t.u1==1 && t.v1==1);
 assert(folio_tile_region(1,1,0,0,512,&t));
 assert(t.w==1 && t.h==1 && t.u0==0 && t.u1==1);
 assert(!folio_tile_region(1200,800,1200,0,512,&t));
 assert(!folio_tile_region(1200,800,0,0,0,&t));
 return 0;
}
