/*
 * Engine scenes image linker script (ENG_LOW_COLD=1, saturn/engine/Makefile): Yaul's ldscripts/yaul.x
 * (MIT, Joe Fenton and Israel Jacquez) plus two low work RAM sections for code and data that never run or are
 * read during a race, and the high work RAM section that carries the first one's load image.
 *
 * .low      cold functions (engine_low_text.ld) and read-only data (engine_low_rodata.ld), linked at
 *           ___low_base in low work RAM. Its load image sits inside .bsshost, and engine_low_init() (first
 *           thing in main) copies it down before anything calls it.
 * .lowbss   cold buffers (engine_low_bss.ld), zeroed by engine_low_init().
 * .bsshost  high work RAM buffers marked ".bss.lowhost.NAME" (engine_closeup.c's tables), placed between .data
 *           and .bss: Yaul's start-up zeroes only ___bss_start..___bss_end, so the BIN's copy of .low survives
 *           there until engine_low_init() copies it down and then zeroes .bsshost (memory lane part 3: the load
 *           image costs no high work RAM and never meets the start-up stack).
 *
 * .low and .lowbss are listed first, and .bsshost before .bss, so that their input sections are claimed before
 * the wildcards of .text and .bss (ld assigns an input section to the first statement that matches it).
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

  /* Host of the .low load image: high work RAM buffers between .data and .bss, so start-up (which zeroes
   * ___bss_start..___bss_end) leaves the image alone; engine_low_init copies .low down, then zeroes them. */
  .bsshost (NOLOAD) :
  {
     . = ALIGN (32);
     ___bsshost_start = .;
     *(.bss.lowhost.*)
     . = ALIGN (16);
     ___bsshost_end = .;
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

  /* The load image of .low: at the start of .bsshost (not zeroed by start-up; engine_low_init zeroes it after
   * the copy). */
  ___low_load = ___bsshost_start;
  ___low_load_end = ___low_load + SIZEOF (.low);
}

ASSERT (___lowbss_end <= ___low_limit, "engine_low.x: the low work RAM block overruns 0x0028BFE0")
/* The listed data really moved (engine_low_rodata.ld names generated sections). */
ASSERT (_fzx_course_ids >= ___low_start && _fzx_course_ids < ___low_end, "engine_low.x: fzx_course_ids is not in .low")
/* The load image fits inside its host buffers, which lie outside the range start-up zeroes. */
ASSERT (___low_load_end <= ___bsshost_end, "engine_low.x: .low is larger than its host buffers (.bsshost)")
ASSERT (___bsshost_end <= ___bss_start, "engine_low.x: .bsshost overlaps the range start-up zeroes")
/* main runs before .low is copied in; it is .text.startup.main, which a name check can miss. (engine_low_init, also
 * run before the copy, is a local symbol that ld cannot name here; engine_low_cold.py always keeps it.) */
ASSERT (_main >= ___low_end || _main < ___low_start, "engine_low.x: main is in .low")
