/* Cold start of the bounded ordinary-retry HOST oracle. */
#include <string.h>
#include "ref.h"
#include "fzx_retry_flow.h"

extern int32_t gRandSeed1, gRandSeed2;
extern uint32_t gRandMask1, gRandMask2;

int ref_retry_race_init(void) {
    const int status = ref_race_init();
    if (status != 0) return status;

    /* Original Menus_Init follows the retained racer and camera initializers.
     * Publish the four live words before the first race update. */
    fzx_retry_rng_t rng = {(uint32_t)gRandSeed1, gRandMask1,
                           (uint32_t)gRandSeed2, gRandMask2, 0, 0};
    if (!fzx_retry_menus_init_rng(&rng)) return 1;
    memcpy(&gRandSeed1, &rng.seed1, sizeof(gRandSeed1));
    memcpy(&gRandSeed2, &rng.seed2, sizeof(gRandSeed2));
    gRandMask1 = rng.mask1;
    gRandMask2 = rng.mask2;
    return 0;
}
