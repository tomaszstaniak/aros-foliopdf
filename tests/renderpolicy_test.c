/* Thumbnail work must not occupy the sole worker between scroll bursts. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/renderpolicy.h"

int main(void)
{
    /* A page can start in a short pause; background thumbnails cannot. */
    assert(render_policy_due(0, 1200, 1000, 1));
    assert(!render_policy_due(1, 1200, 1000, 1));
    assert(!render_policy_due(1, 1999, 1000, 1));
    assert(render_policy_due(1, 2000, 1000, 1));
    /* The next burst resets the quiet interval; a missed tick cannot
     * launch a thumbnail before the movement has been observed. */
    assert(!render_policy_due(1, 2200, 2100, 1));
    assert(!render_policy_due(1, 4000, 2100, 0));
    assert(!render_policy_due(0, 4000, 2100, 0));
    /* Millisecond wrap must not starve work or make it start early. */
    assert(!render_policy_due(1, 499, UINT32_MAX - 499, 1));
    assert(render_policy_due(1, 500, UINT32_MAX - 499, 1));
    puts("renderpolicy_test: all checks passed");
    return 0;
}
