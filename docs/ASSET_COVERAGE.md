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
