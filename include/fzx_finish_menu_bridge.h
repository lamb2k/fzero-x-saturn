#ifndef FZX_FINISH_MENU_BRIDGE_H
#define FZX_FINISH_MENU_BRIDGE_H
#include "fzx_finish_menu.h"
/* Scope and actual fastest-recorded-ghost pointer check. An absent pointer
 * does not establish ghost recording or persistent saving support. */
bool ref_finish_menu_no_recorded_ghost(void);
bool ref_finish_menu_input(fzx_finish_menu_input_t *,uint32_t frame,bool connected);
/* Publish both streams together, only for a successful source navigation
 * tick, with the supplied pre-call state still current. No partial writes. */
bool ref_finish_menu_commit_rng(const fzx_finish_menu_input_t *,const fzx_finish_menu_snapshot_t *);
#endif
