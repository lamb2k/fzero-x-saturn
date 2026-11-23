/* Read-only data in low work RAM (ENG_LOW_COLD, engine_low.x), each checked by hand: nothing reads it during a
 * race frame. The tables are generated, so they are listed by section name (each name is unique in the image).
 *   fzx_course_index  the course bundle's index (8 B a course); the selected row is read from its disc file into
 *                   high work RAM at LOAD (engine_courses.c) and the race reads it through fzx_cid
 *   card_tracks, card_cells   the LOAD cards, copied to VDP2 by the CPU at LOAD
 *   stock_machine   the stock machines, read by machine select (grades) and at LOAD (fzx_machine_tune)
 *   fzx_fx_boosters, fzx_fx_guest_boosters   the race effects' booster rows (ENG_FX): a few halfwords of a row are read a
 *                   frame for each racer drawn; only the scenes image links this file (lane fx-3) */
*(.rodata.fzx_course_index)
*(.rodata.card_tracks)
*(.rodata.card_cells)
*(.rodata.stock_machine)
*(.rodata.fzx_fx_boosters)
*(.rodata.fzx_fx_guest_boosters)
*(.rodata.empty_row)
