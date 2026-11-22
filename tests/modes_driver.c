/* HOST driver for tests/test_modes.py: prints the race mode rules table (saturn/engine/engine_modes.c) for the test to
 * compare with the decompiled source's facts. */
#include <stdio.h>
#include "engine_modes.h"

int main(void) {
    for (unsigned m = 0; m < 8u; ++m) {
        const fzx_mode_rules_t *r = fzx_mode_rules(m);
        if (!r) { printf("mode %u none\n", m); continue; }
        printf("mode %u field %u endless %u pick %u forced %u cup %u record %u avail %u energy %u win %u keeps %u\n", m,
               r->field, r->endless, r->class_pick, r->forced_class, r->cup, r->record, r->avail, r->rival_energy_q8,
               r->win_all_retired, fzx_mode_keeps_times(m));
        for (unsigned d = 0; d < 4u; ++d) printf("class %u %u -> %u\n", m, d, (unsigned)fzx_mode_class(m, d));
    }
    return 0;
}
