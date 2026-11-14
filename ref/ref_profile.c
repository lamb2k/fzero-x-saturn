/* Per-tick function call counts for the G1.3 cost model (built only with PROFILE=1).
 * The decomp objects are compiled with -finstrument-functions; this file is not, so the
 * hooks below are not themselves instrumented. Counts are keyed by function address. */
#include <stdio.h>

#define SLOTS 4096u
static void *keys[SLOTS];
static unsigned counts[SLOTS];

__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *fn, void *site)
{
    (void) site;
    unsigned h = ((unsigned) (unsigned long) fn >> 4) & (SLOTS - 1u);
    while (keys[h] != 0 && keys[h] != fn) h = (h + 1u) & (SLOTS - 1u);
    keys[h] = fn;
    ++counts[h];
}

__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *fn, void *site)
{
    (void) fn;
    (void) site;
}

/* One line per function called this tick: "tick address count"; then the counts restart. */
__attribute__((no_instrument_function)) void ref_profile_tick(FILE *out, long tick)
{
    for (unsigned i = 0; i < SLOTS; ++i) {
        if (counts[i] != 0) {
            fprintf(out, "%ld\t%lx\t%u\n", tick, (unsigned long) keys[i], counts[i]);
            counts[i] = 0;
        }
    }
}
