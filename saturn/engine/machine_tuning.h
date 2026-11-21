#ifndef FZX_MACHINE_TUNING_H
#define FZX_MACHINE_TUNING_H
#include <stdbool.h>
#include "../physics_native.h"
typedef struct { unsigned weight; char body, boost, grip; } fzx_machine_grade_t;
fzx_machine_grade_t fzx_machine_grade(unsigned machine);
bool fzx_machine_tune(FzxPhysCourse *course, unsigned machine, uint32_t slider_bits);
#endif
