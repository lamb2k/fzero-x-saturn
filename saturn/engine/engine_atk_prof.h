/* Diagnostic (ENG_ATK_PROF=1, Saturn only; changes timing slightly): the master's attack and contact
 * sub-steps per frame row, FRT units, summed over the frame's ticks (lane claude/attacks-5).
 * Columns: 0 contact begin and order sort, 1 candidate scan, 2 body poses, 3 pair walk and tests,
 * 4 pushes back to line space and rival damage, 5 the player's step (with contact or plain),
 * 6 fzx_atk_move (driven rivals), 7 sh2z_field_step, 8 CPU attack decisions, 9 checkpoint hash,
 * 10 field ticks stepped before the slave's kick, 11 FRT from the frame's physics start to the kick. */
#ifndef ENGINE_ATK_PROF_H
#define ENGINE_ATK_PROF_H
#include <stdint.h>
#ifndef ENG_ATK_PROF
#define ENG_ATK_PROF 0
#endif
#if ENG_ATK_PROF
#include <yaul.h>
enum { FZX_ATK_PROF_ROWS = 5120u, FZX_ATK_PROF_COLS = 12u };
extern uint16_t fzx_atk_prof[FZX_ATK_PROF_ROWS][FZX_ATK_PROF_COLS];
extern uint32_t fzx_atk_prof_row;
#define APROF_T(v) uint16_t v = cpu_frt_count_get()
#define APROF(k, v) do { const uint16_t q_ = cpu_frt_count_get(); \
    if (fzx_atk_prof_row < FZX_ATK_PROF_ROWS) fzx_atk_prof[fzx_atk_prof_row][k] = (uint16_t)(fzx_atk_prof[fzx_atk_prof_row][k] + (uint16_t)(q_ - (v))); \
    (v) = q_; } while (0)
#else
#define APROF_T(v) do { } while (0)
#define APROF(k, v) do { } while (0)
#endif
#endif
