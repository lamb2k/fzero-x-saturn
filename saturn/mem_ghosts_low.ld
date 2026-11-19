/* FZX_MEM_GHOSTS_LOW=1 only (saturn/Makefile). An added linker script: Yaul's
 * ldscripts/yaul.x (passed by yaul.specs) stays the primary script.
 *
 * Why a linker script and not C: the two arrays are defined in the vendored
 * decomp source (upstream/.../src/game/racer.c), which this port does not edit,
 * and redirecting every access through pointers would change the closure's
 * code. The closure is already compiled with -fdata-sections, so each array is
 * its own input section; this script only claims those two input sections.
 *
 * INSERT BEFORE .bss puts this statement ahead of yaul.x's .bss output section,
 * so these input sections are assigned here before yaul.x's *(.bss.*) wildcard
 * sees them (the link map shows the assignment). The location counter is
 * saved and restored around the low-WRAM section, so .bss continues where
 * .data ended. Addresses must match saturn/mem_low.h. */
SECTIONS
{
  __fzx_mem_saved_dot = .;
  .fzx_ghosts_low 0x002EE5A0 (NOLOAD) : ALIGN (32)
  {
    ___fzx_ghosts_low_start = .;
    KEEP (*ref_closure.o(.bss.gGhosts))
    KEEP (*ref_closure.o(.bss.sGhostReplayRecordingBuffer))
    ___fzx_ghosts_low_end = .;
  }
  . = __fzx_mem_saved_dot;
}
INSERT BEFORE .bss;

ASSERT (___fzx_ghosts_low_start == 0x002EE5A0, "ghost section start moved")
ASSERT (___fzx_ghosts_low_end - ___fzx_ghosts_low_start == 64968, "ghost payload is not 64,968 B")
ASSERT (___fzx_ghosts_low_end <= 0x002FE3A0, "ghost payload overflows its reservation")
ASSERT (_gGhosts >= ___fzx_ghosts_low_start && _gGhosts < ___fzx_ghosts_low_end, "gGhosts not relocated")
ASSERT (_sGhostReplayRecordingBuffer >= ___fzx_ghosts_low_start && _sGhostReplayRecordingBuffer < ___fzx_ghosts_low_end, "recording buffer not relocated")
