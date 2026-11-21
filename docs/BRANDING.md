# F Zero S

Owner direction, 2026-11-17: rename the game **F Zero S**, update its cover and
title screens, and include the two new characters: **Metal Sonic** and
**Dr. Robotnik**, using the classic Eggmobile. This specifically authorizes new
branding and guest artwork; original game asset preservation rules still apply
to the remaining content. Source attribution remains F-Zero X.

| Surface | Required change | Status |
| --- | --- | --- |
| Product/documentation/workbench name | F Zero S | Local naming updated; no hosting deployment |
| Cover | F ZERO S logo, original F-Zero identity plus both guests | Private 1800?2400 illustrated cover and uppercase lettering candidate prepared for owner review |
| Main title | F ZERO S and both guests, readable at native resolution | Guest title integrated in the P3 scenes image; uppercase lettering candidate verified at HOST, pending look call |
| Falcon title variant | Consistent F ZERO S branding and guest presence | Pending |
| Comic title variant | Consistent F ZERO S branding and guest presence | Pending |
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
at 320?224. The lower row shows the candidate before the prompt and with the
controller warning. All four are HOST compositions decoded from their respective
title atlases. Both options use the same existing guest illustrations.

Choose the lettering for the title and cover, and confirm whether the guests
and live prompt are readable at native size. The uppercase candidate changes
only the wordmark slot and keeps the atlas size fixed. Its private generator,
receipt-bound pack and input lineage sit beside the review sheet. The emulator
pad entry in C-113 covered the current title. The uppercase candidate still
needs stage integration, emulator checks and hardware review after selection.
Falcon and comic variants remain open.
