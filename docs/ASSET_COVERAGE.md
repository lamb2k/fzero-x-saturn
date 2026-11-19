# F Zero S asset coverage

Owner direction, 2026-11-17: account for all 3D assets, including the two guest
racers. The product name is **F Zero S**; see [branding](BRANDING.md).

`python tools/asset_coverage.py --output generated/asset-coverage` inventories
the pinned US rev0 source declarations without reading the ROM. The private
JSON and receipt bind every row to its source file and generator. Current census:
1,563 VTX arrays, 203 GFX lists, 1,177 textures, 419 compressed textures and
165 machine LOD entries: 30 stock families plus Super Falcon, Super Stingray
and Super Cat. Arrays/lists are not unique models. Conditional source entries
need an explicit applicability decision, not silent removal.

Every raw census row starts **unqualified**. Existing subsystem evidence below
is not automatically applied to every declaration. This is an inventory baseline,
not a declaration that complete 3D coverage has been achieved.

| Family / use | Established evidence | Remaining coverage |
| --- | --- | --- |
| Stock machines: 30 families, five LODs each | All 30 LOD1 menu models converted; native settings views documented in MACHINE_MENU.md | Race use of every machine/LOD, materials, four skins, damage/boost states and transitions |
| Three extra source machines, five LODs each | Included in census | Applicability, extraction, conversion, native review |
| Blue Falcon race | Original source model and diagnostic race; SOURCE_ART.md | Whole-frame budget and final look; do not generalize to the roster |
| Shadows/reflections | Menu conversion retains source geometry; MACHINE_MENU.md | Per-machine race/menu binding and complete native visual checks |
| Course geometry and materials | Mute City 1: 300 chunks / 4,966 triangles retained; ROAD_BAKE.md | All source courses, branch/join forms, special surfaces and native full-route coverage |
| Scenery and backdrop | Mute City skyline/floor/cloud work documented in SOURCE_ART.md | Every venue, placement, material, animation and occlusion use; C5 remains unresolved |
| Countdown, finish and records 3D/UI | Countdown and initial results native evidence; records HOST continuation | Records native execution; all other podium/ending/menu model consumers |
| Effects and procedural geometry | Incomplete | Boost, exhaust, damage, collision, pit, particles and other source draw paths; declaration scan alone cannot cover runtime geometry |
| Metal Sonic | Private model export and portrait masters exist | Conversion receipts, all views/LODs, race animation/pose, selection/HUD/results and measured native budget |
| Dr. Robotnik / classic Eggmobile | Private model export and portrait masters exist | Same gates, including pilot/vehicle silhouettes; no added racing modifications authorized |

For each semantic asset/use, record source identity, consumers, material/texture
dependencies, variants/LODs, converter/receipt, native binding, representative
views, memory/command/time cost and HOST/EMULATOR/HARDWARE evidence separately.
Retired or inapplicable entries require a reason. Missing geometry, materials or
required variants must fail loading/qualification rather than fall back silently.

Next: join declaration rows to their draw consumers and conversion receipts;
add procedural draw paths and per-course placement inventories. Keep this full-game
ledger alongside the first-course goal; do not require all courses before the
first-course milestone or mistake the first course for full-game coverage.

## Rotation-sprite roster

Codex owns the family-by-family bake, angle/pitch sampling, cell size, race residency, C8 selection and records presentation, and private guest art. Opus owns the offline baker, SH2ZAM rotation-sprite renderer and player proof. Each row starts pending for the rotation-sprite path. Prior menu conversions and guest masters are covered above.

| Machine family | Source | Candidate view profile | Near / far set bytes | Race bake / C8 select / C8 records |
| --- | --- | --- | ---: | --- |
| Blue Falcon | Stock | N20 / F8 | 18,472 / 2,192 | Skin 1 baked / Pending / Pending |
| Golden Fox | Stock | N20 / F8 | 18,456 / 2,256 | Skin 1 baked / Pending / Pending |
| Wild Goose | Stock | N20 / F8 | 18,632 / 2,272 | Skin 1 baked / Pending / Pending |
| Fire Stingray | Stock | N20 / F8 | 18,584 / 2,264 | Skin 1 baked / Pending / Pending |
| White Cat | Stock | N20 / F8 | 18,632 / 2,248 | Skin 1 baked / Pending / Pending |
| Red Gazelle | Stock | N20 / F8 | 18,584 / 2,256 | Skin 1 baked / Pending / Pending |
| Great Star | Stock | N20 / F8 | 18,608 / 2,248 | Skin 1 baked / Pending / Pending |
| Iron Tiger | Stock | N20 / F8 | 18,504 / 2,232 | Skin 1 baked / Pending / Pending |
| Deep Claw | Stock | N20 / F8 | 18,528 / 2,272 | Skin 1 baked / Pending / Pending |
| Twin Noritta | Stock | N20 / F8 | 18,624 / 2,184 | Skin 1 baked / Pending / Pending |
| Super Piranha | Stock | N20 / F8 | 18,512 / 2,256 | Skin 1 baked / Pending / Pending |
| Mighty Hurricane | Stock | N20 / F8 | 18,616 / 2,232 | Skin 1 baked / Pending / Pending |
| Little Wyvern | Stock | N20 / F8 | 18,576 / 2,232 | Skin 1 baked / Pending / Pending |
| Space Angler | Stock | N20 / F8 | 18,616 / 2,248 | Skin 1 baked / Pending / Pending |
| Green Panther | Stock | N20 / F8 | 18,600 / 2,264 | Skin 1 baked / Pending / Pending |
| Black Bull | Stock | N20 / F8 | 18,520 / 2,200 | Skin 1 baked / Pending / Pending |
| Wild Boar | Stock | N20 / F8 | 18,552 / 2,216 | Skin 1 baked / Pending / Pending |
| Astro Robin | Stock | N20 / F8 | 18,616 / 2,240 | Skin 1 baked / Pending / Pending |
| King Meteor | Stock | N20 / F8 | 18,600 / 2,248 | Skin 1 baked / Pending / Pending |
| Queen Meteor | Stock | N20 / F8 | 18,584 / 2,272 | Skin 1 baked / Pending / Pending |
| Wonder Wasp | Stock | N20 / F8 | 18,376 / 2,192 | Skin 1 baked / Pending / Pending |
| Hyper Speeder | Stock | N20 / F8 | 18,616 / 2,208 | Skin 1 baked / Pending / Pending |
| Death Anchor | Stock | N20 / F8 | 18,520 / 2,264 | Skin 1 baked / Pending / Pending |
| Crazy Bear | Stock | N20 / F8 | 18,496 / 2,256 | Skin 1 baked / Pending / Pending |
| Night Thunder | Stock | N20 / F8 | 18,544 / 2,248 | Skin 1 baked / Pending / Pending |
| Big Fang | Stock | N20 / F8 | 18,416 / 2,168 | Skin 1 baked / Pending / Pending |
| Mighty Typhoon | Stock | N20 / F8 | 18,384 / 2,208 | Skin 1 baked / Pending / Pending |
| Mad Wolf | Stock | N20 / F8 | 18,248 / 2,168 | Skin 1 baked / Pending / Pending |
| Sonic Phantom | Stock | N20 / F8 | 18,560 / 2,208 | Skin 1 baked / Pending / Pending |
| Blood Hawk | Stock | N20 / F8 | 18,384 / 2,272 | Skin 1 baked / Pending / Pending |
| Super Falcon | Extra source | N20 / F8 | 18,528 / 2,272 | Skin 1 baked / Pending / Pending |
| Super Stingray | Extra source | N20 / F8 | 18,608 / 2,232 | Skin 1 baked / Pending / Pending |
| Super Cat | Extra source | N20 / F8 | 18,416 / 2,256 | Skin 1 baked / Pending / Pending |
| Dr. Robotnik (classic Eggmobile) | Private guest | N20 / F8 revision 2 | 18,592 / 2,224 | Baked / HOST art / HOST preview |
| Metal Sonic | Private guest | N20 / F8 revision 2 | 18,632 / 2,224 | Baked / HOST art / HOST preview |

Use the player's proof as a starting profile. Compare near rear, banked, side and distant rival views per family before choosing yaw, pitch, mirroring and size. Check asymmetric details and skins. Record the chosen profile and receipt per row. Bind C8 selection and records to the same family identity and check each at its native presentation size.

For each 30-entry race, stream only its machine assets. Budget actual aligned atlas and palette allocations per resident variant, counting shared allocations once. Include peak simultaneous residency during uploads and reserve space for both command/shade banks and other live VDP1 assets. Test the largest permitted field footprint against the VRAM map, guards and 420/70 command limits. Record presented cadence for the full GP and owner look acceptance separately from bake completion.

### First 30-machine residency measurement

HOST bake with RSP1 v1 at `307d280`. The table records complete 8-byte-aligned packs, including cell tables and palettes. Both trial profiles use skin 1, menu LOD1 geometry, explicit left and right views, and one 32-degree pitch row. N20 uses the player's 20 rear-weighted yaw angles with an 18,664-byte ceiling. F8 uses eight equally spaced angles and a 2,304-byte ceiling. The baker fits resolution independently per machine. Selection art, records presentation, other skins, angle coverage and final framing await review. Re-bake on the proposed framing revision.

For a race containing each stock family once, store only those 30 entrant variants on the cartridge. All near and far sets together total **622,984 B** of payload (555,960 near and 67,024 far), before directory and guards. Size the field at race setup from its actual unique variant keys. The two private guests still need model-to-baker adapters.

For VDP1, keep all far sets resident (**67,024 B**) and reserve four near slots (player plus three close rivals). At the largest observed near size of 18,632 B this is **141,552 B**. A fifth slot for one upload in flight raises the reservation to **160,184 B** before guards. Retain each old slot until every submitted list referencing it has retired; draw the resident far set when no safe slot is available. This exceeds the legacy machine region of 18,664 B. E2 needs an engine VRAM map that fits this reservation alongside the track, both command/shade banks, HUD and effects, plus measured transfer time. Slot counts are a planning choice; these are HOST payload sizes.

The Super families have distinct draw-table slots 30â€“32 with their own LOD1 geometry and body colours. Their texture loaders are shared with Blue Falcon, Fire Stingray and White Cat respectively. A private adapter binds these three input rows to the pinned baker. They have no stock character selection slots; select and records still need stable family keys. Skin 1 now covers all 33 source families in both trial profiles. The 66 HOST packs contain **924 cells**. Independent checks validated per-cell offsets, packed extents, palette placement and file integrity, and rejected a cell pointing into its header, overlapping upload/display slots, and an undersized arena.

Across fields of 30 **distinct families** chosen from these 33 measured skin-1 variants, the largest near-plus-far cartridge payload is **623,728 B**. Far sets require at most **67,264 B**, giving conservative VDP1 budgets of **141,792 B** for four near slots or **160,424 B** including one upload slot. Other skins, mixed variants of a family and guest packs need separately measured keys.


### Private guest assets

Both 256-triangle guest models have been baked with the stock rotation-sprite renderer at 45b74b7 (interface 1.1, square-pixel framing revision 2). Each has 20 near and eight far yaw cells at 32 degrees pitch, viewer-relative stock lighting, and one shared 16-entry RGB555 palette per set including transparency. HOST checks decoded all 56 cells and rejected out-of-range cell offsets. The three-dimensional meshes, material definitions, receipts and packs are private.

Both guest keys have a 40×40 selection portrait, 180×245 settings illustration, 80×40 name card, shared 32×32 HUD/results icon and a machine preview rendered from the new mesh using the Records camera. HOST checks cover byte order, transparency, stride and source identity. The race/select/records bindings, on-track review and physical Saturn checks remain open.
