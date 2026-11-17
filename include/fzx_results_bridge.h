#ifndef FZX_RESULTS_BRIDGE_H
#define FZX_RESULTS_BRIDGE_H
#include "fzx_results.h"
/* Reads the completed original tick; never advances or mutates race state.
 * Native connection state is supplied because the source harness has one pad. */
bool ref_results_input(fzx_results_input_t *,uint32_t frame,bool connected);
#endif
