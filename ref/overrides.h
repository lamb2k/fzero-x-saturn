/* Force-included before every decomp file in the reference build (ref/build.sh).
 * MIT; see ../LICENSE.
 *
 * The stock N64 build reads cartridge ROM and RCP registers through raw
 * pointers. Both headers that define those macros have include guards, so
 * including them here first and redefining the macros changes every later
 * use without editing the decomp:
 *   ROM_READ -> the harness ROM image (the owner's ROM, or the CI fixture);
 *   IO_READ  -> 0, an idle bus (the code only polls PI busy bits);
 *   IO_WRITE -> discarded. */
#ifndef FZX_REF_OVERRIDES_H
#define FZX_REF_OVERRIDES_H

#include "libultra/ultra64.h"
#include "macros.h"

unsigned int ref_rom_read32(unsigned int offset);

#undef ROM_READ
#define ROM_READ(addr) ref_rom_read32((unsigned int) (addr))
#undef IO_READ
#define IO_READ(addr) ((void) (addr), 0u)
#undef IO_WRITE
#define IO_WRITE(addr, data) ((void) (addr), (void) (data))

#endif
