# Ordinary retry source integration seam

This source-only closure extracts the bounded US single-player Time Attack
retry schedule. It generates required-call events and the original fade state;
it does **not** implement the native event consumer or prove a second race.
The native finish-menu action request remains the current integration boundary.

Generate with `python tools/retry_flow_source.py`. Generated C, original enums
and their dependency receipt stay under ignored `generated/retry-flow-source`.
Only the extractor, public packet header and HOST tests belong in git.

## Ordering established by source

`src/game/game.c:func_800690FC` consumes `gMenuChangeMode` before calling
`Race_Update`. `Race_Update` first calls `Menus_Update`. Confirmation occurs
later in the draw. Consequently, with confirmation draw at source frame T:

| Source tick | Work |
| --- | --- |
| T+1 | `Menus_Update` dispatches retry; controller/life settlement; race continues |
| T+2 | Game consumes retry; queues hide; hide update 1; race continues |
| T+3 through T+24 | Hide updates 2 through 23; race continues |
| T+25 | Hide update 24 finishes; queues appear; source reset sequence; race update; appear update 1 |
| T+26 through T+45 | Race updates and appear alpha updates 2 through 21 |
| T+46 | Race update; appear finished update clears transition state |

These are source-call positions with zero dispatch delay, not measured pad
latency. The generated alpha follows original `Transition_FadeInit` and
`Transition_FadeUpdate` integer assignments from their float expressions.
The hide transition is 21 alpha updates, two wait updates and one finished
update. Appear is 21 alpha updates and one finished update. The final hide
update and first appear update belong to the **same** source tick.

Begin only from a source-confirmed retry request, with the header's documented
single-player/course/no-ghost-dialog/no-title-demo/no-active-transition
preconditions verified by the real bridge. The API checks frame continuity and
its own bounded state; it cannot inspect hidden source globals. Do not feed
settings, quit, change-machine or change-course into this seam.

## Required consumer and resets

At T+1 execute `func_80095144`: clear each selected controller's `unk_72`, and
honor pending life adjustment in `D_800F80C4`. Do not reinterpret it as reset.
The T+25 ordered events correspond to:

1. `func_8007E08C`, `func_80068DCC` (audio restore/mode), `func_80079F1C`
   (object reset, including its subordinate object/texture/audio state).
2. `func_i3_8012F324` (HUD race/lap/best/flash state).
3. `Controller_Reset` (pending controller read and rumble state).
4. `Racer_Init`, `Camera_Init`, `Effects_Init`, `func_8006D414` (landmine reset).
5. Clear `gGamePaused`; `Menus_Init`; `Hud_ResetLivesChangeCounter`;
   `Hud_InitRacePortraits`.
6. Continue the ordinary race update after its menu dispatch: effects, racer,
   camera, background, course and `func_80074844`, in original order.

`func_i2_80103A70` is extracted directly to preserve reset ordering. Its calls
are emitted as explicit events instead of linked no-ops. Each event must have
a real admitted implementation before a consumer can claim reload. Consumers
must execute the entire packet in order, exactly once, then present its final
fade alpha. A simulation update is still required on every hide tick.

Current reference support already has `Racer_Init` and a guarded camera bridge
(`ref_camera_init(0)` calls original `Camera_Init`). HUD/effects/gadget/object,
full menu initialization and original controller-reset closures are not supplied
by this packet. `Controller_Reset` is not `Controller_ClearInputs`: preserve
held-button edge history and deal with pending input at the original boundary.
Saturn motor/SI handling needs an explicit native adaptation rather than fake
N64 I/O success. Source audio calls remain requests until playback exists.

Reset results/records/finish presentation caches alongside their source owners
without replacing session record storage. `Menus_Init` resets menu state and
calls `RecordsEntry_Init` and `RecordsEntry_InitNameEntry`; it does not clear
the course's record backing. Preserve selected character, skin, engine tuning,
session records and live random streams. Do not call `Course_Init`, CPU setup
`func_i3_80116C4C`, `ref_race_restart`, or restore saved diagnostic RNG words.

## Initial race and live retry RNG are distinct

`fzx_retry_menus_init_rng` extracts the original three slot-speed expressions
and four-by-three slot loops from `Menus_Init`: RNG2, RNG1, RNG2, then 24 RNG2
calls. Its post-call words preserve the original two seed/mask streams. It
does not initialize or reseed either stream, and its counters describe only
this helper's calls.

The current initial `ref_race_init` omits that history. Integration must call
the helper at the real initial `Menus_Init` position after preceding admitted
race initializers, before the first race tick. This changes the initial race
qualification and requires requalification. Calling it at finish would create
the wrong history and cannot repair the existing recorded run.

During retry, execute it at the MENUS_INIT event using the live words **after**
`Racer_Init` and intervening source initializers have run. Advance and publish
both streams together. Do not precompute from the confirmation-frame snapshot
or add the helper twice when a full source `Menus_Init` is eventually linked.
The schedule itself never reads or writes RNG; ordinary updates during hide
may continue to consume both streams.

## HOST evidence and remaining gates

`python3 -m unittest discover -s tests -p test_retry_flow_source.py -v` checks
the 80-tick schedule, all ordered reload events, alpha values, single dispatch,
single reload, duplicate/skipped/wrapped frames and bounded-state rejection.
It checks 25 initialization boundaries with independently calculated modulo
32-bit RNG expectations, including live-word perturbations between races.
Those perturbations model the seam; they do not execute a real race.

GCC and Clang run with UBSan. Eight deliberate controls per compiler change
dispatch order, hide wait, appear duration, controller/racer reset order,
camera reset, reload count, RNG loop count or the live RNG2 seed. Six extraction
drift controls cover changed source anchors. Native compilation, fade visuals,
real ordered reset implementations, held/released pad behavior, retained tuning
and records across an actual second full race, soak and hardware remain unrun.

## Selected-race cold start

The HOST arm `FZX_RETRY_INIT=1 FZX_SOURCE_MACHINE_MENU=1 FZX_SOURCE_CAMERA=1 bash ref/build.sh gcc -O2`
builds a separate first-race executable. Use the owner course record, ROM boot
data and selected-machine input script in the normal reference driver. The arm
selects Blue Falcon skin 1 with engine 0.5078125, runs source race and camera
initialization, and applies the extracted Menus_Init RNG work once, before
any input tick. It rejects a diagnostic restart prefix.

For the 7,059-input selected race, GCC and Clang produced byte-identical HOST
traces. At the entry to the first ref_tick, all four RNG words matched the
independent source Math_Rand oracle. Bypassing the menu RNG helper in the
debugger changed all four words. The race-state hash alone was insensitive
to this omission. A separate HOST probe fed the finish menu a synthetic records-ready snapshot
after the first race. It drove real controller edges through the finish menu,
committed its RNG, and confirmed RETRY at frame 7119. At 7120 the schedule
emitted the controller/life settlement before the ordinary race update. Running
the source settlement cleared the selected controller marker; suppressing
that call preserved the marker and failed the check on both compilers. Course
load count stayed one. Live records entry, native dispatch, the T+25 reload
and the second race remain open.
