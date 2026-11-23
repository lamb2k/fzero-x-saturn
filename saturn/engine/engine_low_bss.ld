/* Cold buffers in low work RAM (ENG_LOW_COLD, engine_low.x), each checked by hand: nothing reads or writes them
 * during a race frame. The sources mark them with a ".bss.lowcold.NAME" section (LTO renames plain static
 * buffers, so their section names are not stable):
 *   texture_cd.c   kept            the CD root list, read by loads
 *   road_native.c  entries         the road pack load's root list
 *   engine_records.c table         the records table, read and written by the scenes
 *   engine_drive.c pack_buf        the physics pack, parsed once at LOAD by fzx_phys_load
 *   engine_field.c pack_buf        the rivals pack, copied out once at LOAD by fzx_field_load (ENG_ATTACKS images only)
 * Not here: anything the SCU DMA or a CD transfer reads or writes (VDP1 command lists, sector buffers), and the
 * backup library's buffers (bup, records io). */
*(.bss.lowcold.*)
