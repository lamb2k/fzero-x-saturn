/* Cold buffers in low work RAM (ENG_LOW_COLD, engine_low.x), each checked by hand: nothing reads or writes them
 * during a race frame. The sources mark them with a ".bss.lowcold.NAME" section (LTO renames plain static
 * buffers, so their section names are not stable):
 *   texture_cd.c   kept            the CD root list, read by loads
 *   road_native.c  entries         the road pack load's root list
 *   engine_records.c table         the records table, read and written by the scenes
 *   engine_drive.c pack_buf        the physics pack, parsed once at LOAD by fzx_phys_load
 *   engine_courses.c practice_rows Practice's rival rows, read when the race's field is built
 *   engine_ghost_race.c ghost_rec, ghost_play   the Time Attack ghost's recording and kept run: touched once every
 *                   second tick (about 12 B), the one place low work RAM is used in a race frame (the 24-course
 *                   image with the modes does not link with them in high work RAM)
 *   engine_fx.c    fx_*       the race effects' buffers (ENG_FX; lane fx-3): the two jobs' command buffers and depth lists, the
 *                   staged cars and sparks, the spark ring, the square-root table (the CPU only: the master stages, either SH-2
 *                   emits, the master merges; a few hundred bytes are touched in a frame, measured +0.014 ms on Sand Ocean 1)
 *   engine_closeup.c fx_base_pal   the load's body palettes (ENG_FX), copied when a rival's table is first built
 *   engine_scenes.c menu_heap      the menu scenes' arena (ENG_FX only: with the effects the high block would not keep the
 *                   master's headroom); nothing reads it in a race
 * Not here: anything the SCU DMA or a CD transfer reads or writes (VDP1 command lists, sector buffers), and the
 * backup library's buffers (bup, records io). */
*(.bss.lowcold.*)
