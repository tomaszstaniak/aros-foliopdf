#include <assert.h>
#include "../src/gpupolicy.h"
int main(void) {
 assert(!folio_gpu_renderer(0)); assert(!folio_gpu_renderer(""));
 assert(!folio_gpu_renderer("Mesa softpipe"));
 assert(!folio_gpu_renderer("LLVMPIPE (LLVM 15)"));
 assert(!folio_gpu_renderer("Software Rasterizer"));
 assert(!folio_gpu_renderer("unknown"));
 assert(folio_gpu_renderer("AMD Radeon RX 580"));
 assert(folio_gpu_renderer("virgl"));
 assert(!folio_gpu_image_bytes(0,4,100));
 assert(!folio_gpu_image_bytes(100,100,299));
 assert(folio_gpu_image_bytes(10,20,30)==600);
 assert(folio_gpu_rect_fits(800,600,20,70,750,500));
 assert(!folio_gpu_rect_fits(700,540,20,70,750,500));
 assert(!folio_gpu_rect_fits(800,600,-1,0,10,10));
 assert(!folio_gpu_rect_fits(800,600,20,70,2147483647,500));
 return 0;
}
