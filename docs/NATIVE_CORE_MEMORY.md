# Unused core renderer storage

`FZX_NATIVE_CORE_EMPTY=1` bypasses the old ribbon renderer when the original
simulation, native road tiles and original machine are all selected. Those
layers already make its polygon list empty. The core still publishes the same
count, speed, lap and flags; course setup, pose tracking and steering hints are
unchanged. Native source renderers continue to supply the visible scene.
The flag defaults off and invalid feature combinations fail at build time.

This is a prerequisite for countdown integration: the first combined link
overflowed high work RAM by 3,328 bytes. Bypassing the unused transform and
sort path lets the compiler remove its code and scratch without moving DMA
commands or source simulation state into slower memory.

| Linked allocation | Before | After |
| --- | ---: | ---: |
| Text | 290,760 B | 284,408 B |
| Data | 12,176 B | 12,168 B |
| BSS | 716,480 B | 663,104 B |
| Total | 1,019,416 B | 959,680 B |

The linked allocation falls by 59,736 bytes. The flag-off binary is identical
to the admitted compact-cloud image. GCC and Clang UBSan checks retain exact
frame headers and steering hints over 1,024 synthetic poses, both with and
without a source camera. A deliberately nonempty frame fails that check.

Ymir checks pass at 13 held views through the same 685-input source replay.
All source packets, submitted VDP1 commands, their referenced texture/palette/
shade bytes and both VDP1 framebuffers are identical. Composite differences
are confined to the CPU/MAX/SIM/MISS diagnostic digits. Minimum untouched
stack space rises from 5,936 to 65,680 bytes in those captures. This exceeds
16 KB for this sample; full-route and combined-candidate stack qualification
remain separate gates. No frame-rate or HARDWARE acceptance follows from it.

Run `python3 tests/test_native_core_empty.py` under Linux. Private evidence is
`host/countdown/core-empty-{0,1}` and `emu/countdown/core-empty-comparison.json`
under `$FZX_EVIDENCE`. This is EMULATOR memory/scene evidence; hardware is UNRUN.
