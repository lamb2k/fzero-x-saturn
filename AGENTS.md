# Agent rules for fzero-x-saturn

This is the one agent file. `CLAUDE.md` only points here.

## Boot set

At the start of every session read, in order, and nothing else until a card needs it:

1. this file;
2. `docs/STATUS.md` (current state, under 80 lines);
3. `docs/HANDOFF.md` (the next card and open threads, under 50 lines);
4. the one gate section of `docs/POC_PLAN.md` you are working in.

Old checkpoint prose reads like instructions. History lives in git, not in these files.

## Goal

A one-track proof of concept of F-Zero X on Saturn: Mute City 1, Blue Falcon, one racer,
the original physics in a fixed-point core, 60 fps target, 4 MB RAM cartridge required.
Tier T (technical) is the commitment; tier P (playable) follows. See `docs/POC_PLAN.md`.

## How work is done

- One card at a time, to done. Done means committed, pushed, gated, and the card's
  negative control shown failing. "Coded, unrun" is a valid status and must be written so.
- Fix ordinary defects without asking. If the same named gate fails after two distinct
  hypotheses, record the blocker and the exact unblock condition in STATUS, then move to the
  next dependency-safe card. Never loosen a tolerance to pass.
- Divergence hunts: find the first divergent tick, then the function that wrote it. Fix there.
- Prototype handling stays behind `FZX_SIM=proto` and is named on screen. It is never
  reported as the port. The default flips to `fixed` only when the closed-loop gate passes.
- No general engine: no libultra layer, no display-list interpreter, no general
  float-to-fixed translator. Title-specific code only.
- Renderer and platform changes leave the simulation trace STRICT-identical.
- Every optimisation lands behind a knob that defaults off, with knob-off identity, one cost
  arm and one STRICT gate.

## Evidence

Tiers never promote each other: **HOST**, **reference**, **EMULATOR**, **HARDWARE** (owner
only; UNRUN until then), **MODELLED** (any estimate, and any frame-cost number from an emulator
timer). Every claim in STATUS carries its tier.

Local evidence lives outside git in `$FZX_EVIDENCE`, one subfolder per lane:
`ref/`, `track/`, `sim/`, `saturn/`, `emu/`, `web/`. Retail-course traces, stills and logs stay
there; only hashes and counts are committed.

Generated files live in `generated/` (ignored) and carry a receipt
(`<file>.receipt`, sha256 of generator and outputs). Builds refuse stale or hand-edited
output; regenerate, never edit. `tools/check_receipt.sh <receipt>` checks one.

Reports lead with playable extent in plain words, then gates passed, failed and unrun with
their tiers, then image and commit identities, then the next card.

## Repository safety

This repository is public; every push is a publication.

- Never commit ROMs, extracted course, model or texture data, retail-course traces, or stills
  of retail content. The ROM path comes from `FZX_TEST_ROM` and never appears in git, CI logs
  or chat.
- Run `python3 tools/hygiene_scan.py` before every push. CI runs it too.
- Stage named paths only: no `git add -A`, no force-push, no history rewrites.

## Stop and ask only for

- a missing ROM path for a ROM-lane card when no ROM-free card remains;
- the owner touchpoints in `docs/POC_PLAN.md` section 7;
- anything outward-facing (publishing stills, hosting builds);
- an unresolved material architecture choice that Gate 1's rules do not already decide.

## Commands

```sh
git submodule update --init third_party/SH2ZAM
make test CC=gcc BUILD=build/gcc && make test CC=clang BUILD=build/clang
make sanitize CC=clang
python3 tools/hygiene_scan.py
make web && python3 web/test-browser.py          # needs Playwright
# Saturn image: see README "Build for Sega Saturn" (pinned Yaul docker image)
```
