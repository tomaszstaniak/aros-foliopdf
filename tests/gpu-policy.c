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
 return 0;
}
