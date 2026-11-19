/* FZX_MEM_GHOSTS_LOW=1 only (saturn/Makefile), passed as an implicit linker
 * script after the objects. Yaul's ldscripts/yaul.x (-T, from yaul.specs)
 * stays the primary script and is not edited or replaced.
 *
 * Why a linker script and not C: the two arrays are defined in the vendored
 * decomp source (upstream/.../src/game/racer.c), which this port does not
 * edit, and redirecting every access through pointers would change the
 * closure's code.
 *
 * Why renamed sections and not INSERT: this binutils refuses
 * "INSERT BEFORE .bss" against a -T script (".bss not found for insert";
 * INSERT augments only the built-in default script). So the Makefile copies
 * ref_closure.o with exactly two input sections renamed,
 * .bss.gGhosts -> .fzxlow.gGhosts and .bss.sGhostReplayRecordingBuffer ->
 * .fzxlow.sGhostReplayRecordingBuffer (still NOBITS). yaul.x's .bss wildcards
 * (*(.bss) *(.bss.*)) can no longer match them, and only this statement does.
 * The section is NOLOAD at a fixed low-WRAM address, appended after yaul.x's
 * statements, so .bss, .uncached, ___bss_end and ___end are computed by
 * yaul.x exactly as before. Addresses must match saturn/mem_low.h. */
SECTIONS
{
  .fzx_ghosts_low 0x002EE5A0 (NOLOAD) :
  {
    ___fzx_ghosts_low_start = .;
    KEEP (*(.fzxlow.gGhosts))
    KEEP (*(.fzxlow.sGhostReplayRecordingBuffer))
    ___fzx_ghosts_low_end = .;
  }
}

ASSERT (___fzx_ghosts_low_start == 0x002EE5A0, "ghost section start moved")
ASSERT (___fzx_ghosts_low_end - ___fzx_ghosts_low_start == 64968, "ghost payload is not 64,968 B")
ASSERT (___fzx_ghosts_low_end <= 0x002FE3A0, "ghost payload overflows its reservation")
ASSERT (_gGhosts >= ___fzx_ghosts_low_start && _gGhosts < ___fzx_ghosts_low_end, "gGhosts not relocated")
ASSERT (_sGhostReplayRecordingBuffer >= ___fzx_ghosts_low_start && _sGhostReplayRecordingBuffer < ___fzx_ghosts_low_end, "recording buffer not relocated")
