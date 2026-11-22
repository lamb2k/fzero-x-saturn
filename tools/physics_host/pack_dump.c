/* HOST pack row dump: loads a pack with saturn/physics_native.c and prints what the reader decoded (force card tests,
 * tools/physics_pack_force_test.py). Output is one line: rc, force_frac, legacy, the four force constants, accel_force, the
 * size fzx_phys_save writes for the pack's own state (0 when it refuses). MIT; see ../../LICENSE. */
#include <stdio.h>
#include <stdlib.h>

#include "../../saturn/physics_native.h"

static FzxPhysCourse course;
static uint8_t image[1 << 20], out[1 << 20];

int main(int argc, char** argv) {
    size_t n;
    int rc;
    FILE* f;
    if (argc < 2 || (f = fopen(argv[1], "rb")) == NULL) return 2;
    n = fread(image, 1, sizeof(image), f);
    fclose(f);
    rc = fzx_phys_load(&course, image, n, 1);
    printf("rc=%d force_frac=%d legacy=%d force_low=%d force_slide=%d force_high=%d top_force=%d accel_force=%d save=%zu\n", rc,
           course.force_frac, course.legacy_force, (int) course.vehicle.force_low, (int) course.vehicle.force_slide,
           (int) course.vehicle.force_high, (int) course.vehicle.top_force, (int) course.start.accel_force,
           rc == 0 ? fzx_phys_save(&course, image, &course.start, course.prev_buttons, out, sizeof(out)) : (size_t) 0);
    return 0;
}
