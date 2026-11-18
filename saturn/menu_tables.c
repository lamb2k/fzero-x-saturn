/* Compiled only by tools/menu_tables_assets.py, never linked into the image.
 * Its four .rodata.fzx_*_images sections are the menu image tables exactly as
 * the SH-2 compiler lays them out; the tool extracts them into MENUTAB.PAK.
 * The meta table records each element size and count so the tool can check
 * count x size against the extracted section. */
#include <stdint.h>
#include "machine_menu_identity.h"
#include "finish_menu_identity.h"
#include "records_identity.h"
#include "course_menu_identity.h"

#define FZX_MENU_TABLE_META(t) (uint32_t)sizeof((t)[0]), (uint32_t)(sizeof(t) / sizeof((t)[0]))
__attribute__((used, section(".rodata.fzx_menu_tables_meta")))
static const uint32_t fzx_menu_tables_meta[8] = {
    FZX_MENU_TABLE_META(fzx_machine_menu_images), FZX_MENU_TABLE_META(fzx_finish_menu_images),
    FZX_MENU_TABLE_META(fzx_records_images), FZX_MENU_TABLE_META(fzx_course_menu_images),
};
/* Keeps the four static tables (and only their own sections) in the object. */
__attribute__((used))
static const void *const fzx_menu_tables_keep[4] = {
    fzx_machine_menu_images, fzx_finish_menu_images, fzx_records_images, fzx_course_menu_images,
};
