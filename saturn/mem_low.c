/* FZX_MEM_GHOSTS_LOW=1 only: boot set-up and checks of the low-WRAM ghost
 * reservation (saturn/mem_low.h, saturn/mem_ghosts_low.ld). */
#include <stdbool.h>
#include <string.h>
#include "mem_low.h"

/* ___fzx_ghosts_low_{start,end} in mem_ghosts_low.ld (the SH ABI adds one underscore). */
extern unsigned char __fzx_ghosts_low_start[], __fzx_ghosts_low_end[];

volatile uint32_t fzx_mem_ghosts_status;
volatile uint32_t fzx_mem_ghosts_checks;

static bool guards_intact(void) {
    const volatile uint32_t *lo=(const volatile uint32_t *)(uintptr_t)FZX_MEM_GHOSTS_RESERVE_BASE;
    const volatile uint32_t *hi=(const volatile uint32_t *)(uintptr_t)FZX_MEM_GHOSTS_GUARD_HIGH;
    for(unsigned i=0;i<8u;++i)if(lo[i]!=FZX_MEM_GUARD||hi[i]!=FZX_MEM_GUARD)return false;
    return true;
}

/* Once, at the top of main(), before any low-WRAM scratch user: the section is
 * NOLOAD, so crt0 does not clear it. Zero the whole payload room (the arrays'
 * BSS start state) and write both guards. Never called again: the ghost
 * arrays keep their contents across races, as BSS does. */
void fzx_mem_ghosts_boot(void) {
    const uintptr_t start=(uintptr_t)__fzx_ghosts_low_start,end=(uintptr_t)__fzx_ghosts_low_end;
    if(start!=FZX_MEM_GHOSTS_START||end-start!=FZX_MEM_GHOSTS_EXPECTED_BYTES||
       end>FZX_MEM_GHOSTS_GUARD_HIGH){fzx_mem_ghosts_status=2;return;}
    memset((void *)FZX_MEM_GHOSTS_START,0,FZX_MEM_GHOSTS_PAYLOAD_BYTES);
    volatile uint32_t *lo=(volatile uint32_t *)(uintptr_t)FZX_MEM_GHOSTS_RESERVE_BASE;
    volatile uint32_t *hi=(volatile uint32_t *)(uintptr_t)FZX_MEM_GHOSTS_GUARD_HIGH;
    for(unsigned i=0;i<8u;++i){lo[i]=FZX_MEM_GUARD;hi[i]=FZX_MEM_GUARD;}
    fzx_mem_ghosts_status=1;
}

/* Bounded and side-effect free apart from the two report words. */
uint32_t fzx_mem_ghosts_check(void) {
    ++fzx_mem_ghosts_checks;
    if(fzx_mem_ghosts_status==2)return 2;
    if(!guards_intact())fzx_mem_ghosts_status=3;
    return fzx_mem_ghosts_status;
}
