# Original finish menu and retry

The first closure prepares the original US Time Attack finish-menu state and
five action requests: retry, settings, change machine, change course and quit.
It does not yet display this menu on Saturn or perform a race reload.

Begin on the same source tick that records becomes ready, retaining that tick's
controller input. The first draw decreases the reveal timer from 60 to 59. Capture
selection and reveal before navigation; confirmation follows navigation. Keep
the original stick thresholds, D-pad wrap, disabled-ghost skip and 60/75 repeat.
Every eligible navigation tick consumes both original random streams, even with
neutral input. The source bridge checks the original controller port and live
RNG words before publishing both resulting streams together.

This first closure admits the no-recorded-ghost path only. The bridge checks the
actual fastest-recorded-ghost pointer; it does not infer a ghost from the race
time. Ghost recording and persistent saving remain unimplemented. A non-null
pointer fails this bounded scope. Audio events remain requests without playback.
An action request freezes this state until a separate consumer handles it.

HOST checks pass under GCC and Clang with UBSan. The extracted state transcript
matches the uncut original drawer with inert graphics calls. Tests cover reveal,
thresholds/repeat, all five requests, simultaneous move/confirm, pre-navigation
draw state, disconnects and frame boundaries. Seven state mutations and seven
bridge mutations fail, including omitted RNG2, stale RNG2 and partial writeback.
The actual recorded records-ready snapshot enters at frame 7860 with reveal 59,
selection 0, retained input and no RNG consumed; the prior nonready snapshot and
a delayed first step fail unchanged. Both compilers produce the same snapshot.

Private evidence: `host/finish-menu-state/host-tests.json` and `seam/receipt.json`.
The seam receipt independently binds its two HOST rows to existing native
captures. An older seal's qualification-metadata hash is stale; the changed
metadata's history is unknown. The receipt records that discrepancy without
resealing it or claiming historical immutability of the whole stream. No new
emulator run, target timing or hardware acceptance is established by this card.

The source asset converter preserves 24 CI8 labels and the complete 235-entry
Font5 mapping in 133 unique images. Its 38,368-byte pack and 12,160-byte static
metadata reproduce byte-for-byte on Windows and Linux. Seven HOST tests on each
platform pass; an independent 46,272-pixel audit preserves RGB5, transparency
and crops. The receipt accepts the real pack and rejects byte-flipped, truncated
and altered-header controls. The selected label retains the original animated
32-color palette. Native residency and palette presentation are checked separately below.

The read-only draw closure captures the original menu rectangles, six labels,
Font5 cursor, animated colors and reveal scissor into bounded packets. HOST
checks cover 36,000 combinations (120 frames, five selectable rows, 60 reveal
states) under GCC and Clang with UBSan, with identical output and unchanged input.
Six deliberately broken variants per compiler fail: scissor, disabled color,
glyph scale, glyph ID, label ID and frame identity. Invalid reveal states and
null inputs fail without publishing output. Windows and Linux generated C is
byte-identical. Private qualification is in `host/finish-menu-draw/receipt.json`.
The generated-file receipt accepts the output and rejects an edited copy.
These HOST checks do not establish native texture sampling, palette presentation
or backdrop composition; the native checks below cover those boundaries.

## Native palette qualification

The default-off native implementation uses the original label and Font5 pack,
three 32-word label palettes and a current-frame records backdrop. The action
request remains frozen; this implementation does not perform ordinary retry.

The first native menu capture stopped at input 7860 before its palette upload.
The guard expected CRAM mode 1, but SDK VRAM initialization had selected mode 0.
The preceding 26 held states passed source and guard comparisons; their minimum
sampled stack was 21,232 bytes. This partial capture does not qualify the menu.

The correction admits the observed RGB555 mode 0 with 1,024 independent words.
It writes only indices 256..287, 512..543 and 768..799. Mode 0 mirrors these into
the upper CRAM window; the comparator checks both copies and preserves every
other word, including fog index 1 and debug-font indices 1008..1023. It retains
the VBlank, sprite format, priority and palette-offset checks. GCC/Clang HOST
checks pass, including eleven rejected scene mutations. The updated disabled arm
matches the original 1,002,276-byte executable exactly; a changed-byte control
is rejected.

The corrected image completed 8,062 inputs, 73 held captures and three diagnostic
resets. Source state, draw packets, pose, camera, both random streams and guards
pass; minimum sampled stack is 21,232 bytes. All 47 menu boundaries pass exact
command, resident texture, displayed label index/RGB5 and protected-CRAM checks;
eight corruption controls fail as intended. The helper observes the current draw
before the callback, including frozen actions, and verifies the exact source tuple,
one palette commit and no pending upload. This fixes stale callback capture in
the earlier helper without changing the game image or source oracle.

The current-frame records backdrop passes 64 held views against independent
GCC/Clang command reconstruction, including resident textures/palettes and the
actual VDP1 partition bounds. Separate HOST span controls reject bad extents and
reproduce the earlier records capture exactly. The historical records metadata
seal discrepancy remains recorded; this does not claim a historical binary seal.

This EMULATOR qualification uses the isolated C3 stream renderer, whose measured
performance remains a no-go; see [native stream](ROAD_STREAM_NATIVE.md). Both
features default off. Native image BIN SHA256:
`6979bcd0701c2671ebe07154b33ddb4811fcb8aa6fbc18b2ade4c4094908040e`.
Two malformed-pack controls pass: a one-byte payload change fails the hash check,
and a changed directory size fails the size check. Both stop before source/menu
state or commands are published, with low-RAM and cartridge guards intact.
Local inspection finds readable labels, but diagnostic HUD text overlaps the
records view. Final appearance,
ordinary retry, performance and HARDWARE acceptance remain open. Diagnostic
resets do not establish ordinary retry.

## Ordinary retry still to integrate

Original draw confirmation sets a request; the following menu update dispatches
it. The game-mode state machine runs the original hide transition, reloads race
state and starts the appear transition. It does not use the diagnostic restart,
which restores initial RNG words and reruns CPU initialization. Ordinary retry
retains the live streams and machine/tuning/session records while source
initializers reset racers, camera, effects, HUD, menus and input state.

The existing bounded race bridge omits the original initial `Menus_Init` random
history: one RNG1 and 26 RNG2 calls in Time Attack. Correct this at the real race
initialization boundary and requalify the affected run; do not add those calls
at finish. Current results/records evidence covers the bounded bridge, not the
entire cartridge's original initialization sequence.

Remaining: final presentation cleanup, original dispatch/fades/reload,
a second ordinary race, other menu destinations,
source audio/effects, pad-only completion, soak and owner/hardware review.
