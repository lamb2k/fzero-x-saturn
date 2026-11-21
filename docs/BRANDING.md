# F Zero S

Owner direction, 2026-11-17: rename the game **F Zero S**, update its cover and
title screens, and include the two new characters: **Metal Sonic** and
**Dr. Robotnik**, using the classic Eggmobile. This specifically authorizes new
branding and guest artwork; original game asset preservation rules still apply
to the remaining content. Source attribution remains F-Zero X.

| Surface | Required change | Status |
| --- | --- | --- |
| Product/documentation/workbench name | F Zero S | Local naming updated; no hosting deployment |
| Cover | F ZERO S logo, original F-Zero identity plus both guests | Private 1800x2400 illustrated cover and uppercase lettering approved in O-128 |
| Main title | F ZERO S and both guests, readable at native resolution | Guest title integrated in the P3 scenes image; uppercase lettering candidate verified at HOST, approved under O-128 |
| Falcon title variant | F ZERO S and both guests | Existing composition accepted per O-129 |
| Comic title variant | F ZERO S on the original comic background | O-129 guest-free composition staged and verified at HOST |
| Title input/warnings | Preserve pad behavior and readable prompts | Original prompt, credits and warning slots preserved in the uppercase candidate; connected, pre-prompt and disconnected HOST stills prepared |
| Selection/HUD/results | Both guests use consistent portraits/name cards | Private guest UI masters prepared; both playable identities persisted through reboot in P3 emulator checks. Final look remains open. |

Private guest masters are the identity references. Preserve Metal Sonic's blue
metal body/red eyes/yellow chest ring and Robotnik's classic glasses/moustache,
red outfit and Eggmobile. Keep Captain Falcon and the source racing identity
prominent. Do not invent a redesigned vehicle or replace source roster artwork.

The current first-title converter uses a 304x240 background and 160x65 logo;
source placement is 320x240, cropped by eight lines at top/bottom for 320x224.
Review every variant at native size, preserve prompt contrast, and regenerate
receipt-bound packs. Validate logo/text spelling, both guest silhouettes, alpha,
palette/VRAM limits, transitions and controller/no-controller views in emulator.
Art masters and converted banks remain private. A cover preview does not prove
native title integration. Existing source symbols, repository path and FZX_* APIs
remain stable internal identifiers; credits retain their original source names.

## Main-title look call

The owner review sheet at `$FZX_EVIDENCE/host/codex-c123-brand/owner-title-review.png`
compares the current mixed-case wordmark and the uppercase **F ZERO S** candidate
at 320x224. The lower row shows the candidate before the prompt and with the
controller warning. All four are HOST compositions decoded from their respective
title atlases. Both options use the same existing guest illustrations.

On 2026-11-21 the owner approved uppercase **F ZERO S** for the main title
and cover, both guests and PUSH START at 320x224, and the red controller
warning as shown. The selected private cover is
`$FZX_EVIDENCE/host/codex-c123-brand/F Zero S - illustrated cover.png`.

Run `python3 tools/stage_guest_titles.py "$FZX_EVIDENCE"` in the build
checkout before the live check. The scenes default reads the selected main
pack from `generated/title_guest`. Its atlas remains 186,944 bytes.
Check connected, pre-prompt and disconnected states, then enter with the
pad when the coordinator grants a slot.

## Falcon and comic look call

O-129 retains the Falcon composition shown in
`$FZX_EVIDENCE/host/codex-c124-brand/owner-title-variants.png`. The owner
requested the original comic panels without guest illustrations. The comic
pack now uses the original converted background, the approved uppercase
wordmark and the original layout, including the warning at (80, 162) before
the native crop. Its current 320x224 HOST sheet shows the pre-prompt, PUSH START
and disconnected states at `$FZX_EVIDENCE/host/codex-c126-brand/comic-title-states.png`.

Select each staged pack for a separate build with `ENG_TITLE_ART=title_guest_falcon`
or `ENG_TITLE_ART=title_guest_comic`.

Each variant pack adds 7,680 bytes over the main pack and fits separately in the
192 KiB title allocation. The private build script, per-variant receipts and
individual stills sit alongside the sheet. Runtime selection, transitions,
controller input and hardware appearance await integration and a granted run.
