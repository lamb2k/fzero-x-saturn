# Next Saturn hardware disc set

Build from the integrated `engine` commit containing memory4 (SMPC quiet sections), the Jack GP catalog, lf5-final and the approved main/comic pack staging. Record the actual merge commits, engine SHA and `saturn/engine/sh2zam.pin` with the set. Use the ordinary player pad, six-course bundle, 29 rivals, Standard GP catalog and both guest bits. Keep fault injection off. The owner's numbered routes are in the [hardware sheet](HARDWARE_TEST.md#next-console-session).

| Folder | Build difference | Console purpose |
| --- | --- | --- |
| `01-main-sound-on` | Shipping player configuration, approved main title, sound on | Items 2, 3 and 4. Full-loop route, music during transitions, six Jack courses and GP next-round load. Item 5 if this is the confirmed balanced-rails image. |
| `02-main-sound-off` | Same player build with sound compiled off | Item 1. Exercise the memory4 quiet section during records save/load and reboot. Keep the same mixed-mode track layout as disc 01 so silence comes from the program configuration. |
| `03-comic-title` | Disc 01 with the O-129 comic atlas | Item 4. Three prompt states, original comic panels, uppercase lettering, original warning placement and pad entry. |
| `04-cadence` (diagnostic) | Sound-on player build with the hardware counter | Full-loop and item 5 WORST/OVER2 photos. Verify the merged counter actually displays advancing race and menu blocks. The old full-loop disc had extra menu instrumentation; the presence of the make flag alone proves neither readout. Mark unavailable counters on the sheet. |
| Road comparison, only if requested | Matched copies of the counter build on the confirmed balanced-rails pack with the two named line-start settings | Item 5. Reuse disc 04 for its matching setting; label the other with its actual setting. If the road decision is pending, issue the other discs and leave item 5 pending. |

### Build and package from engine

The commands below are for the builder when the merges land. In a fresh private WSL tmpfs gate stage, use the usual six-course generated seed with current receipts and pinned SH2ZAM. Use a dedicated keeper, at least 30 GB free on C:, and one build job. Set `STAGE` to the prepared `claude-gate-*` stage name, `EVIDENCE` to the private evidence root, `MUSIC` to the gate's complete CDDA directory containing `manifest.json`, `OUT` to a new local evidence directory for this batch, and `SET` to a new private packaging directory. Use paths visible to WSL. Run from the repository root at the selected engine commit. Stage that exact tree under `w/` using the gate's normal init/lib/tree steps before running these commands.

```bash
set -euo pipefail
: "${STAGE:?}" "${EVIDENCE:?}" "${MUSIC:?}" "${OUT:?}" "${SET:?}"
rev=$(git rev-parse HEAD)
work="${FZX_YAUL_IMAGE:-$HOME/yaul-image}/$STAGE/w"
gate=tools/engine_gate/stage.sh
mkdir -p "$OUT" "$SET"
git show "$rev:saturn/engine/sh2zam.pin" | cmp - "$work/saturn/engine/sh2zam.pin"
python3 "$work/tools/stage_guest_titles.py" "$EVIDENCE"
python3 tools/disc_cdda.py --music "$MUSIC" --ids all --header-only --header "$OUT/cdda_tracks.h"
cmp "$OUT/cdda_tracks.h" "$work/generated/cdda/cdda_tracks.h"
bash "$gate" receipts "$STAGE" > "$OUT/receipts.txt"
if grep '^STALE ' "$OUT/receipts.txt"; then exit 1; fi

player=(FZX_E1_DRIVE=1 ENG_SCENES=1 E1_DRIVER=0 ENG_P3_UI=1
        ENG_COURSE_SELECT=1 FZX_CUP_DIAG_CATALOG=0 FZX_GUEST_MASK=25769803776
        ENG_LOW_COLD=1 ENG_SMPC_OVERLAP_CONTROL=0 ENG_RIVAL_BEHAVE=1 ENG_CDDA=0)
disc() {
    local label=$1
    shift
    bash "$gate" build "$STAGE" "$OUT" "$label" --jobs=1 "${player[@]}" "$@"
    python3 tools/disc_cdda.py --iso "$OUT/$label/fzs-engine.iso" --music "$MUSIC" \
        --ids all --name fzs-engine --output "$SET/$label" > "$OUT/$label/disc.json"
    (cd "$SET/$label" && sha256sum -- *.cue *.bin > SHA256SUMS && sha256sum -c SHA256SUMS)
}
disc 01-main-sound-on ENG_SND=1 ENG_TITLE_ART=title_guest
disc 02-main-sound-off ENG_SND=0 ENG_TITLE_ART=title_guest
disc 03-comic-title ENG_SND=1 ENG_TITLE_ART=title_guest_comic
disc 04-cadence ENG_SND=1 ENG_TITLE_ART=title_guest E0_HW_COUNTER=1
```

Use the merged road default for the shipping images. If a road comparison is commissioned, verify the baked Sand Ocean pack receipt against the coordinator's selected balanced-rails pack. Use the same `disc` command as 04 with a distinct label and explicit `E0_LINE_START=0` or `1` for each specified arm. Record the setting used by 04 before reusing it. The overlap-control hang image is a lane test and has no role on the owner's disc set.

Record the engine and SH2ZAM revisions, effective flags (including merged road defaults), input pack receipts, program BIN and ISO hashes, CUE hash, track count and each disc's manifest hash in the handoff. Check each CUE's FILE entries resolve locally, track 01 is MODE1/2352 and every audio track matches the compiled music table. Compare audio-track hashes across all discs. Deliver the CUE and its named tracks together. Keep maps, symbols and build logs in private evidence.

### Second-emulator smoke before delivery

Use Mednafen's Saturn core, a matching BIOS, a 4 MB expansion and a pad on port 1. Open each **final packaged CUE** (for example `mednafen "$SET/01-main-sound-on/fzs-engine.cue"`). Record Mednafen version, settings, disc manifest hash, outcome and private capture path per image.

- On 01, cold boot to the main title, enter SELECT by pad, confirm Jack/Standard is offered, start a race and listen for music and effects. Check both guest identities on the grid in separate starts.
- On 02, verify silence, save distinctive initials to a qualifying record, wait for saving to finish, restart the emulator with the same backup memory, and reopen the record.
- On 03, capture the comic title before PUSH START, with the prompt, and on a disconnected-pad boot. Check the warning position, then verify connected pad entry.
- On 04 and any road comparison disc, reach Sand Ocean, capture the named bend or pipe approach and verify which overlay blocks advance. Record loader, picture, input or audio failures against the exact CUE. Repeat checksum verification after copying the package. Console latency, SMPC behaviour and cadence stay HARDWARE UNRUN until the owner tests them.

### README.md for the owner's Desktop folder

Copy the text below to the root of the new dated set under `Desktop/F-Zero Saturn hardware discs/`. Include a copy of the matching hardware sheet named `HARDWARE_TEST.md` beside it. Keep the existing E0 soak and earlier full-loop folders available separately. Fill the blanks from the finished package and omit any optional disc not supplied.

---

# F ZERO S — next Saturn console session

Use a 60 Hz Saturn, a 4 MB RAM cartridge and a pad in port 1. Open each folder's `fzs-engine.cue` on your loader and keep all its named track files together. These discs contain private game assets. Record your console, loader, firmware and the disc ID on the [test sheet](HARDWARE_TEST.md#next-console-session).

| Disc | What to try | CUE SHA-256 | SHA256SUMS file SHA-256 |
| --- | --- | --- | --- |
| 01 Main, sound on | Items 2–4: film cold boot and reset through the first TITLE pixel; check both guests from selection into a race; photograph the main title before the prompt, with PUSH START, and with the pad disconnected. Then use the full-loop route for sound and Jack GP. | ______ | ______ |
| 02 Main, sound off | Item 1: save a qualifying time with distinctive initials, reopen RECORDS, reboot after the save settles and check the same course, name and time. Silence is expected. | ______ | ______ |
| 03 Comic title | Item 4: repeat the three title photographs and check Start enters SELECT. Look for the uppercase wordmark over the original comic panels, with no guest overlays. | ______ | ______ |
| 04 Cadence | Photograph the race and menu counters during the full-loop route. For item 5, use the confirmed Sand Ocean road configuration and record WORST/OVER2 before and after the same bend or pipe approach. | ______ | ______ |
| Optional matched road disc: ______ | Item 5: repeat the same course, machine and camera on the supplied second setting. Mark pending if no configuration has been named. | ______ | ______ |

Engine commit ______ · SH2ZAM pin ______ · Sand Ocean pack/line-start setting per disc ______ · Mednafen smoke result per disc ______

Each disc folder's `SHA256SUMS` contains the SHA-256 of the CUE and every track. Keep your console captures with the disc ID and note the first screen or action where anything went wrong.
