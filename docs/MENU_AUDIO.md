# Menu audio contract (engine side), DRAFT 1

Lane: claude/menu-audio. Status: contract only, nothing bound yet. Tiers: every number below is EMULATOR (Ymir) or
read from the code; nothing has run on a Saturn.

The front end (Codex) calls these functions from its screens. The engine (this lane) owns the sound RAM, the CD
player and the order of CD commands. All functions are in `saturn/engine/engine_snd.h`, exist only when
`ENG_SND_MENU=1` (default 1 on the scenes image when `ENG_SND=1`; `ENG_SND_MENU=0` is the control and builds the
image as it is today), and are called from the manager's frame (main-loop context), never from an interrupt.
None of them waits for the CD block or the SCSP; each is cheap (register writes or a few words of state).

## What the front end sees

```c
/* Boot, once, while the disc is free (right after the title packs are read in boot_enter). Reads SNDMENU.PAK
 * (the four effects, about 86 KB) into sound RAM and the disc's TOC. scratch: the 4,096 B CD workspace. On
 * failure the menu stays silent (all calls below become no-ops, status in engine_snd_menu_report[0]); it never
 * faults the boot. */
bool engine_snd_menu_load(void *scratch, uint32_t scratch_bytes);

/* Music. music_id is the game's own music number (include/sfx.h BgmId): ENG_SND_BGM_TITLE 14 (CD track 12),
 * ENG_SND_BGM_SELECT 15 (track 13); any id the disc has a track for is accepted (16 option, 17..24 jingles).
 * ENG_SND_BGM_NONE (0xFF) = fade out and pause. It records what the screen wants; the engine does the rest:
 *   - same id already playing or paused for a disc read: nothing happens (call it every frame if convenient);
 *   - another id playing: the old one fades (8 frames), then the new one starts from its beginning;
 *   - nothing playing: it starts (CD commands only, no disc data read) as soon as the drive is quiet and
 *     no disc hold is active.
 * Call it from a screen's enter hook. Music is stopped by the engine at LOAD and at RACE leave
 * (engine_snd_leave): the next screen's enter hook asks again. */
void engine_snd_menu_music(uint32_t music_id);

/* Effects. na_se_id is the game's own system SE number (Audio_TriggerSystemSE). In this pack:
 *   30 ENG_SND_SE_CURSOR  cursor moves (inst 24)        16 ENG_SND_SE_BACK    back / cancel (inst 17)
 *   33 ENG_SND_SE_SELECT  accept (inst 40)              62 ENG_SND_SE_START   title Start / mode confirm (inst 64)
 * It plays at the next frame boundary on slots 20..23 (oldest replaced when all four are busy); an id that is
 * not in the pack is counted and ignored. Safe to post on the same frame as request(LOAD): effects keep
 * playing through LOAD's blocking reads (they are on their own slots and engine_snd_leave does not stop them). */
void engine_snd_menu_se(uint32_t na_se_id);

/* Disc hold, for any menu code that reads the disc while a menu screen is up (SETTINGS.PAK ranges, grid packs...).
 * Music and CD data reads cannot share the drive. */
bool engine_snd_disc_acquire(void);   /* first call: fade the music (6 frames) and pause the drive; returns true
                                       * once the drive is quiet and the disc may be read. Call it every frame
                                       * until it returns true (a preload step returns "not done" meanwhile). */
void engine_snd_disc_release(void);   /* after the last read of the group, and on preload cancel; idempotent, safe
                                       * when nothing is held. The music resumes where it paused. */

/* Once per manager frame outside RACE: replaces engine_snd_idle's body (engine_snd_idle calls it). The only
 * place that sends CD commands, plays queued effects, ages them and drives the music state. */
void engine_snd_menu_frame(void);
```

## When it is safe to call what

| Call | From | Not from |
| --- | --- | --- |
| `engine_snd_menu_load` | boot_enter, after `title_cache` | anything after the first menu frame (the disc is no longer free) |
| `engine_snd_menu_music` | any menu screen's enter or tick hook | RACE, LOAD (LOAD's `engine_snd_leave` owns the music there) |
| `engine_snd_menu_se` | any menu tick (cursor, accept, back), including the frame that requests LOAD | RACE (the race has its own events); interrupts |
| `engine_snd_disc_acquire` / `release` | a preload step or a tick that is about to read the disc | RACE; LOAD (its own fade does the same job) |

Rules the front end follows:
1. Every disc data read in a menu screen sits between `engine_snd_disc_acquire() == true` and `engine_snd_disc_release()`.
   The loader counts any read made while music is not quiet (`engine_snd_disc[1]`); the gate expects 0.
2. A preload that is cancelled calls `engine_snd_disc_release()` in its cancel hook.
3. One cursor move = one `engine_snd_menu_se(30)`; do not post an effect from a held/repeating key more often than the
   screen's own repeat rate.
4. Menu music ids: title screen asks TITLE (14), every other front-end screen asks SELECT (15) (Codex's reading of the
   source: mode, course, machine and settings use SELECT). RESULTS and RECORDS: unchanged until Codex says.

## Engine behaviour behind it

- Music is CD audio (tracks already on the disc: title track 12, select track 13). The TOC is read once at boot and kept
  (about 400 B of low work RAM); no TOC read happens in a menu.
- Hold: fade the CD input level to silence over 6 frames, then Seek-to-pause. The pause position is kept. After
  release the engine sends one Play Disc from that position (to the track end, then the loop range, as the race music
  does). No fixed delays: every step waits on CMOK or the periodic CD status.
- LOAD and race leave: unchanged (`engine_snd_leave`): fade, pause, music state cleared, queued effects kept.
- Effects: 8-bit PCM clips (from the ROM's sound font, converted locally, never committed) in sound RAM at
  0x61180 (inside the 126,592 B free region of the race map), loaded once at boot from the disc file SNDMENU.PAK.
  Race images and the race clip pack are untouched.

## Memory and cost (read from the code; measured numbers go in the report)

| Resource | Change |
| --- | --- |
| Sound RAM | +86 KB (397,696 B used now, about 483 KB of 512 KB after); inside the free region, checked by a static assert |
| High work RAM (master stack headroom) | about +16 B (the call from `engine_snd_idle` and `engine_snd_leave`); code, state and tables go to low work RAM (.low / .lowbss) |
| Low work RAM | code about 1.5 KB, state and TOC about 0.5 KB, from the unused path-pack block |
| Disc | one new file, SNDMENU.PAK (about 86 KB), made by `tools/sfx_menu_pack.py` |
| CPU | a menu frame adds about 40 register-sized steps; a blocking TOC read happens once at boot |

## Counters for captures (`engine_snd_menu_report[8]`)

[0] status (1 ok, 2 pack load failed, 3 TOC failed), [1] effects posted, [2] effects played, [3] effects dropped (unknown
id or ring full), [4] music requests, [5] music starts, [6] resumes after a hold, [7] holds acquired.
`engine_snd_disc[0]/[1]` (existing): disc data reads / reads made while music was not quiet.

## Open questions for Codex

- Do you want the refusal buzz (NA_SE_32, the locked-item sound) and the settings reset sound (NA_SE_5)? Both need a
  small extension (NA_SE_32 is a gated loop). Say so and the pack grows; otherwise v1 is the four above.
- Does any of your five screens read the disc while the screen is up (SETTINGS.PAK range reads, grid packs)? If so
  each needs acquire/release as above. Names of the reads and their size per screen would let me test the longest.
