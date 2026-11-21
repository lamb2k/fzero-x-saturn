/* Read-only data in low work RAM (ENG_LOW_COLD, engine_low.x), each checked by hand: nothing reads it during a
 * race frame. The tables are generated, so they are listed by section name (each name is unique in the image).
 *   fzx_course_ids  the course bundle; LOAD copies the selected row into high work RAM (engine_courses.c) and the
 *                   race reads that copy through fzx_cid
 *   card_tracks, card_cells   the LOAD cards, copied to VDP2 by the CPU at LOAD
 *   stock_machine   the stock machines, read by machine select (grades) and at LOAD (fzx_machine_tune) */
*(.rodata.fzx_course_ids)
*(.rodata.card_tracks)
*(.rodata.card_cells)
*(.rodata.stock_machine)
