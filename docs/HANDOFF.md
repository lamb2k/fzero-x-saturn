# Handoff

Keep under 50 lines. Overwrite, do not append.

## Next card

G1.5 then G1.4: emulator lane and first boot (Gate 1, effort high). G1.1 census and G1.2 reference can run beside them.

## Open threads

- G0.4: the Saturn HUD line `SIM PROTO (PROTOTYPE, NOT ORIGINAL)` is unverified on screen; check it in the first-boot still (G1.4).
- Local clang in the agent container lacks ASan runtimes; `make sanitize CC=gcc` works here, CI runs clang.

## Fields still fed from the reference

- Not applicable until Gate 3.
