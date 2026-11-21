/*
 * Engine scenes image linker script (ENG_LOW_COLD=1, saturn/engine/Makefile): Yaul's ldscripts/yaul.x
 * (MIT, Joe Fenton and Israel Jacquez) plus two low work RAM sections for code and data that never run or are
 * read during a race.
 *
 * .low      cold functions (engine_low_text.ld) and read-only data (engine_low_rodata.ld), linked at
 *           ___low_base in low work RAM and loaded with the program after ___end: the BIN carries the section
 *           above the user heap's control block, and engine_low_init() (first thing in main) copies it down
 *           before anything calls it.
 * .lowbss   cold buffers (engine_low_bss.ld), zeroed by engine_low_init().
 *
 * Both are listed first so that their input sections are claimed before .text's and .bss's wildcards
 * (ld assigns an input section to the first statement that matches it).
 */

OUTPUT_FORMAT ("elf32-sh")
OUTPUT_ARCH (sh)
EXTERN (_start)
ENTRY (_start)
SEARCH_DIR ("$YAUL_INSTALL_ROOT/$YAUL_ARCH_SH_PREFIX/lib");

MEMORY {
  ram (Wx) : ORIGIN = 0x06004000, LENGTH = 0x000FC000
}

/* Low work RAM block (scenes image): the path pack's header uses 0x00200000 up; the block's top stays below the
 * path reservation's sentinels (0x0028BFE0) and the CD scratch at 0x0028C000. */
___low_base = 0x00240000;
___low_limit = 0x0028BFE0;

SECTIONS
{
  .low ___low_base : AT (___low_load)
  {
     ___low_start = .;
     INCLUDE engine_low_text.ld
     . = ALIGN (4);
     INCLUDE engine_low_rodata.ld
     . = ALIGN (16);
     ___low_end = .;
  }

  .lowbss ALIGN (___low_end, 16) (NOLOAD) :
  {
     ___lowbss_start = .;
     INCLUDE engine_low_bss.ld
     . = ALIGN (16);
     ___lowbss_end = .;
  }

  .text :
  {
     *(.text)
     *(.text.*)
     *(.gnu.linkonce.t.*)

     INCLUDE ldscripts/yaul-c++.x

     . = ALIGN (4);
  } > ram

  .rodata :
  {
     . = ALIGN (16);

     *(.rdata)
     *(.rodata)
     *(.rodata.*)
     *(.gnu.linkonce.r.*)
  } > ram

  .data :
  {
     . = ALIGN (16);

     *(.data)
     *(.data.*)
     *(.gnu.linkonce.d.*)
     SORT (CONSTRUCTORS)
     *(.sdata)
     *(.sdata.*)
     *(.gnu.linkonce.s.*)
  } > ram

  .bss :
  {
     . = ALIGN (16);
     PROVIDE (___bss_start = .);

     *(.bss)
     *(.bss.*)
     *(.gnu.linkonce.b.*)
     *(.sbss)
     *(.sbss.*)
     *(.gnu.linkonce.sb.*)
     *(.scommon)
     *(COMMON)

     . = ALIGN (16);
     PROVIDE (___bss_end = .);
  } > ram

  .uncached (0x20000000 | ___bss_end) : AT (___bss_end)
  {
     *(.uncached)
     *(.uncached.*)

     . = ALIGN (4);
  }

  /* Back to cached addresses */
  ___end = ___bss_end + SIZEOF (.uncached);

  /* The load image of .low: past the user heap's control block, which Yaul's __mm_init writes at ___end before
   * main (engine_low_init checks the gap against the block's real size). */
  ___low_load = ALIGN (___end + 0x2000, 16);
  ___low_load_end = ___low_load + SIZEOF (.low);
}

ASSERT (___lowbss_end <= ___low_limit, "engine_low.x: the low work RAM block overruns 0x0028BFE0")
/* Before main the master stack (0x06100000 down) holds only Yaul's start-up frames. */
ASSERT (___low_load_end <= 0x060FC000, "engine_low.x: the .low load image reaches the start-up stack")
