/* Quiet-time policy, independent of MUI and the renderer. */
#ifndef FOLIO_RENDERPOLICY_H
#define FOLIO_RENDERPOLICY_H
#include <stdint.h>

static int render_policy_due(int thumbnail, uint32_t now, uint32_t last_move, int unchanged)
{
    /* A short pause is enough for a visible page, but wheel events arrive
     * in bursts. Decoding a thumbnail can delay cancellation for seconds. */
    return unchanged && (uint32_t)(now - last_move) >= (thumbnail ? 1000u : 150u);
}
#endif
