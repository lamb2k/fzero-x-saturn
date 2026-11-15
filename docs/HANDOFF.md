# Handoff

Limit: 50 lines. Replace this file each session; do not append.

## Last session (2026-11-14)

Gate 0 landed: agent file, state files, hygiene scan with CI job, generation
receipts with a build-side check, and the visible `FZX_SIM` selector. See
[STATUS.md](STATUS.md) for each card's evidence.

## Next card

**G1.1 closure census** (plan section 6, Gate 1; effort high). Needs the
decomp submodule:

```sh
git submodule update --init upstream/G-Diffuser
git -C upstream/G-Diffuser submodule update --init decomp
```

Start from `Racer_Update` for one human racer. Split the census by source
file across subagents; review the merged table yourself.

## Notes for the next agent

- The Saturn HUD SIM line is built but nobody has seen it: the image has
  never been booted. Check it at G1.4 first boot.
- Generated files now need their receipt. An old `generated/course.h` from
  before Gate 0 has none, so the imported Saturn build refuses it. Re-run the
  importer.
- The importer writes the receipt as bytes with LF line ends, so the shell
  check works on Windows checkouts too. Do not switch it to text mode.
