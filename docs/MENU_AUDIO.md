# Menu audio contract (engine side), DRAFT 2

Lane: claude/menu-audio. Draft 2 carries Codex's amendments (C-181, O-186). Tiers: every number below is EMULATOR (Ymir)
or read from the code; nothing has run on a Saturn.

The front end (Codex) calls these functions from its screens. The engine owns the sound RAM, the CD player and the
order of CD commands. All functions are in `saturn/engine/engine_snd.h`, exist only when `ENG_SND_MENU=1` (default 1 on
the scenes image when `ENG_SND=1`; `ENG_SND_MENU=0` is the control and builds the image as it was), and are called from
the manager's frame (main-loop context), never from an interrupt. None of them waits for the CD block or the SCSP.

## API

```c
/* Boot, once, while the disc is free (right after title_cache in boot_enter). Reads the TOC and SNDMENU.PAK (six effects,
 * 86,712 B) into sound RAM. The two halves fail separately: no TOC = no menu music, no pack = no effects; the other half
 * keeps working and the boot never faults (status in engine_snd_menu_report[0]: 1 ok, 2 pack failed, 3 TOC failed).
 * Synchronous (a blocking TOC read and a pack read), like the title art reads. scratch: the 4,096 B CD workspace. */
bool engine_snd_menu_load(void *scratch, uint32_t scratch_bytes);

/* Music: the game's own music id (ENG_SND_BGM_TITLE 14 = CD track 12, ENG_SND_BGM_SELECT 15 = track 13,
 * ENG_SND_BGM_OPTION 16 = track 14, any id the disc has a track for; ENG_SND_BGM_NONE 0xFF = fade out and pause).
 * It records what the screen wants. Idempotent: call it from every enter hook, or every frame.
 *   - same id playing, or paused by a hold: nothing (a hold's release resumes it);
 *   - another id playing: the old one fades (8 frames), then the new one starts from its beginning;
 *   - nothing playing: it starts (CD commands only) once the drive is quiet and no hold is active.
 * A newer request always wins over a pending resume. */
void engine_snd_menu_music(uint32_t music_id);

/* Effects: the game's own sound-effect numbers. Plays at the next manager frame on slots 20..23 (oldest replaced).
 *   30 CURSOR  cursor moves                 16 BACK    cancel / back
 *   33 SELECT  accept                       62 START   title Start; a Time Attack mode confirm posts 62 then 33
 *   32 REFUSE  refused selection: a looping buzz, keyed off by the engine after 24 fields (about 0.4 s) or at LOAD / race leave
 *   22 BALANCE one tick each time the integer tenth of the balance changes (the same note of the same instrument as the
 *              original's player-channel 22, so it is one clip for both channels)
 * An id not in the pack is counted (report[3]) and ignored. Posting on the frame that requests LOAD is safe: the
 * effects start before LOAD's blocking reads and keep playing through them (they are not stopped by engine_snd_leave). */
void engine_snd_menu_se(uint32_t na_se_id);

/* Disc hold, around every synchronous disc read in a menu screen (cache misses only; hits need none). */
bool engine_snd_disc_acquire(void);   /* poll every frame: the first call requests a fade (6 frames) and a pause; repeated
                                       * polls are one hold; true once the drive is quiet. Returns true at once when there
                                       * is no music of ours to stop (failed TOC, nothing playing), so navigation continues. */
void engine_snd_disc_release(void);   /* after the last read of the group, before cartridge-to-VRAM work; and in every
                                       * preload-cancel hook; idempotent, safe when nothing is held. The music resumes from
                                       * where it paused if that track is still the one wanted. */

/* Once per manager frame outside RACE (engine_snd_idle calls it): CD commands, effect playback and ageing, music state. */
void engine_snd_menu_frame(void);
```

## When it is safe to call what

| Call | From | Not from |
| --- | --- | --- |
| `engine_snd_menu_load` | boot_enter, after `title_cache` | after the first menu frame |
| `engine_snd_menu_music` | any menu screen's enter or tick hook | RACE, LOAD (`engine_snd_leave` owns the music there) |
| `engine_snd_menu_se` | any menu tick, including the frame that requests LOAD | RACE (it has its own events), interrupts |
| `engine_snd_disc_acquire` / `release` | a preload step or tick that is about to read the disc | RACE; LOAD (its own fade does the same job) |

## Music map and disc reads (Codex, C-181)

| Screen | Music | Disc read on a cache miss |
| --- | --- | --- |
| Title | 14 | none after boot |
| Mode (pages) | 15 | PAGESUI.PAK 278,768 B |
| Cup / course | 15 | CUPUI.PAK 241,664 B |
| Machine grid | 15 | MACHGRID.PAK 246,332 B |
| Stock settings | 15 | one range of SETTINGS.PAK, 183,224 to 199,112 B |
| Guest settings, paint, balance, cursor | 15 | none |
| Options | 16 when bound | per Codex |

Rules: (1) every read in a menu screen sits between `engine_snd_disc_acquire() == true` and `engine_snd_disc_release()`;
(2) a cancelled preload releases; (3) the loader counts any read made while music is not quiet (`engine_snd_disc[1]`):
the gate expects 0; (4) one producer per UI action when the front end's binding replaces the reference binding.

## Engine behaviour

- Music is CD audio (tracks already on the disc). The TOC is read once at boot and kept (408 B of low work RAM).
- Hold: the CD input level fades to silence over 6 frames, then Seek-to-pause; the pause position is kept when the pause
  completes (before any data read can disturb the periodic status). After the release the engine sends one Play Disc from
  that position (to the track end, then the loop range, as the race music does) if the wanted track is still the held one.
  Every step waits on CMOK or the periodic CD status; there are no fixed delays.
- LOAD and race leave (`engine_snd_leave`): fade, pause, music state cleared (a pending resume is cancelled), queued
  effects dropped, looping effects stopped; one-shot effects keep ringing.
- Effects are 8-bit PCM clips from the ROM's sound font (converted locally by `tools/sfx_menu_pack.py`, never committed) in
  sound RAM at 0x61180..0x76438, loaded once at boot from the disc file SNDMENU.PAK. The race clip pack and race images
  are untouched.

## Memory and cost

| Resource | Change (EMULATOR build, scenes-drv image) |
| --- | --- |
| Sound RAM | +86,712 B at 0x61180 (inside the 126,592 B free region; a static assert and the pack tool check it): 484,408 B used of 524,288 |
| High work RAM | none gained: the high bss end moved down 2,272 B with the change (the code and state are in .low / .lowbss); master stack unused on the scenes image is in the gate's memory row |
| Low work RAM | .low +4,096 B (block rounding; the menu code, effect table), .lowbss +about 0.5 KB (state, TOC 408 B), well inside the block |
| Disc | SNDMENU.PAK, 86,712 B |
| CPU | a menu frame adds a few dozen register-sized steps; boot adds one TOC read and the pack read |

## Counters (`engine_snd_menu_report[16]`, in low work RAM)

[0] status, [1] effects posted, [2] played, [3] dropped, [4] music requests, [5] fresh starts, [6] resumes, [7] holds
acquired, [8] state (0 stopped, 1 playing, 2 pausing, 3 paused), [9] FAD where the pause left the track, [10] last pickup
FAD, [11] music owned, [12] music wanted, [13] hold. `engine_snd_disc[0]/[1]`: disc data reads / reads made while music
was not quiet.

## Reference binding (in engine_scenes.c, replaced by the front end's binding)

Boot: `engine_snd_menu_load` after `title_cache`. Title enter: music 14; Start: effect 62. Mode, select enter: music 15.
Select: cursor moves 30, balance tenth 22, accept 62 then 33, refused accept 32, back 16.
