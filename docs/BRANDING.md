# F Zero S

Owner direction, 2026-11-17: rename the game **F Zero S**, update its cover and
title screens, and include the two new characters: **Metal Sonic** and
**Dr. Robotnik**, using the classic Eggmobile. This specifically authorizes new
branding and guest artwork; original game asset preservation rules still apply
to the remaining content. Source attribution remains F-Zero X.

| Surface | Required change | Status |
| --- | --- | --- |
| Product/documentation/workbench name | F Zero S | Local naming updated; no hosting deployment |
| Cover | F ZERO S logo, original F-Zero identity plus both guests | Private 1800x2400 illustrated cover and uppercase lettering candidate prepared for owner review |
| Main title | F ZERO S and both guests, readable at native resolution | Guest title integrated in the P3 scenes image; uppercase lettering candidate verified at HOST, pending look call |
| Falcon title variant | F ZERO S and both guests | Receipt-verified HOST stills ready for the owner |
| Comic title variant | F ZERO S and both guests | Receipt-verified HOST stills ready for the owner |
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

Choose the lettering for the title and cover, and confirm whether the guests
and live prompt are readable at native size. In the disconnected still, check
the red controller warning against the background: its contrast is low. The uppercase candidate changes
only the wordmark slot and keeps the atlas size fixed. Its private generator,
receipt-bound pack and input lineage sit beside the review sheet. The emulator
pad entry in C-113 covered the current title. The uppercase candidate still
needs stage integration, emulator checks and hardware review after selection.
## Falcon and comic look call

The private sheet at `$FZX_EVIDENCE/host/codex-c124-brand/owner-title-variants.png`
shows the main, Falcon and comic titles at 320x224 before the prompt, with
PUSH START, and with the controller warning. Each variant uses its original
320x240 background, the uppercase wordmark candidate and both guest masters.
The original layout places the comic logo lower and gives the two variants
different warning heights. The HOST frames use those placements and the
existing native static warning.

The owner can judge all three together. On Falcon, check whether the guest
portraits leave the machine prominent. On comic, check whether both guests
read against the lower panels. Check warning readability in all three rows.
Lettering and both variant compositions await owner approval.

Each variant pack adds 7,680 bytes over the main pack and fits separately in the
192 KiB title allocation. The private build script, per-variant receipts and
individual stills sit alongside the sheet. Runtime selection, transitions,
controller input and hardware appearance await integration and a granted run.
