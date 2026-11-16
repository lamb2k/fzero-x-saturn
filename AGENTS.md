# Agent rules

This file is the one agent file for this repository. The plan is
[docs/POC_PLAN.md](docs/POC_PLAN.md); its section 8 has the full operating
rules and the reasons for them. This page is the short form.

## Boot set

At the start of every session read, in this order, and nothing else first:

1. this file;
2. [docs/STATUS.md](docs/STATUS.md): where the work stands;
3. [docs/HANDOFF.md](docs/HANDOFF.md): what the last session left;
4. the one gate section of the plan you are working in.

Read source files when a card needs them. Old checkpoint prose reads like
instructions and pulls work off course.

## Work

- **Owner clarification, 2026-11-16: Saturn preservation takes priority over exact parity.**
  Approximate behavior or similar visual/audio quality is authorized when exact
  source behavior is impractical on Saturn. Preserve the game's identity and
  playable experience; document adaptations and compare them locally. Do not
  block useful native implementations on bit-exact rendering or request repeated
  permission for these adaptations. Source assets remain the starting material;
  placeholders and missing features do not become completed content.
  Asset clarification: prebake conversions offline; assets should remain original
  or be made Saturn-compatible. Approximation does not authorize replacement art.
- **Owner direction, 2026-11-15: the real game on Saturn, preserving source art.**
  Original machine models, track materials, scenery, effects, HUD/fonts, menus
  and audio are port content, extracted locally from the owner's ROM. Placeholder
  art is for synthetic tests only and cannot satisfy a playable-port milestone.
  Preserve source appearance and behavior through native conversion; do not
  replace them with newly designed assets. See POC_PLAN section 4.7.
- One card at a time, to done. Done means committed, pushed and gated, with
  the card's control shown failing. "Coded, unrun" is a valid status; write it.
- A failing gate gets two distinct hypotheses tested. Then record the blocker
  and its exact unblock condition in STATUS and move to the next
  dependency-safe card. Never loosen a tolerance to pass.
- Divergence: find the first divergent tick and field, then the function
  that wrote it. Fix there.
- Source owns simulation and state; the native layer owns presentation.
  Optimisations land behind a knob that defaults off, with knob-off identity.
- No general engine: no libultra layer, no display-list interpreter, no
  general float-to-fixed translator.

## Evidence

- Tiers never promote each other: HOST, EMULATOR, HARDWARE, MODELLED.
  Emulator frame timings are MODELLED. HARDWARE is UNRUN until the owner runs it.
- Evidence lives outside git in `$FZX_EVIDENCE`, one subfolder per lane
  (`host/`, `emu/`, `rom/`, `cost/`). Commit hashes and counts, never
  retail-derived traces, stills or data.
- Generated files carry a receipt (`*.receipt`, written by the generator).
  Builds refuse output that does not match it. Regenerate; never hand-edit.
  Re-check any odd bug on a clean build first.
- The simulation in use is always visible (`FZX_SIM`, shown on the Saturn
  HUD and in the web workbench). The prototype driver is never the port.

## Repository safety

This repository is public: every push is a publication.

Owner authorization (2026-11-15): push completed, evidenced source-only changes to
`origin/gate1` regularly. This authorizes routine pushes after the gates and hygiene
scan; it does not authorize publishing ROM-derived content or releases.

- Run `python3 tools/hygiene_scan.py` before every push. CI runs it too.
- Never commit ROMs, extracted course, model or texture data, retail-course
  traces or stills of retail content. The ROM path is `FZX_TEST_ROM`, set
  locally; it never enters the repo, a CI log or a chat.
- Stage named paths only. No `git add -A`, no force-push, no history rewrites.

## Ask the owner only for

The three touchpoints in plan section 7, a missing ROM path when no ROM-free
card remains, anything outward-facing, and an architecture choice the Gate 1
rules do not already decide.

## Keep the state small

`docs/STATUS.md` stays under 80 lines and `docs/HANDOFF.md` under 50. History
lives in git. Trim in the same commit that would push either over its limit.
