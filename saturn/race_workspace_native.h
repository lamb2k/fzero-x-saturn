#ifndef FZX_RACE_WORKSPACE_NATIVE_H
#define FZX_RACE_WORKSPACE_NATIVE_H
#include <stdbool.h>
#include <stdint.h>
/* Full source GfxPool, CPU-only in the already-required 4 MiB cartridge.
 * Report: magic,status,start,bytes,end,guard-before,guard-after,road-end. */
extern volatile uint32_t fzx_race_workspace_report[8];
bool fzx_race_workspace_load(void);
bool fzx_race_workspace_guards(void);
#endif
