/* Platform shims for the float reference (card G1.2). MIT; see ../LICENSE.
 *
 * The reference compiles the decomp's own racer, course, camera and math code natively.
 * These replace N64 platform services it calls during init and the tick. They do not
 * change game logic: addresses are already host pointers. The ROM DMA hook that serves
 * the course record is in ref_game.c, which can see the decomp's PI structures.
 */
#include "ref.h"

extern void bcopy(const void*, void*, unsigned int);

unsigned char ref_course_record[REF_COURSE_RECORD_SIZE];
unsigned int ref_dma_course_loads;
unsigned int ref_dma_other_loads;
int ref_player_lives_increments;

unsigned int osVirtualToPhysical(void *address) { return (unsigned int) address; }
void *osPhysicalToVirtual(unsigned int address) { return (void *) address; }
unsigned int Segment_SegmentedToVirtual(unsigned int address) { return address; }

/* ROM words for ROM_READ: the first 4 KB of a US rev0 ROM in big-endian (z64) order when
 * --rom is given, else the fixture's authored constant. ref_rom_reads counts the reads. */
unsigned char ref_rom_head[REF_ROM_HEAD_BYTES];
int ref_rom_loaded;
unsigned int ref_fixture_rom_word = REF_FIXTURE_ROM_WORD;
unsigned int ref_rom_reads;

unsigned int ref_rom_read32(unsigned int offset)
{
    ++ref_rom_reads;
    if (!ref_rom_loaded || offset + 4 > REF_ROM_HEAD_BYTES) {
        return ref_fixture_rom_word;
    }
    return ((unsigned int) ref_rom_head[offset] << 24) | ((unsigned int) ref_rom_head[offset + 1] << 16) |
           ((unsigned int) ref_rom_head[offset + 2] << 8) | ref_rom_head[offset + 3];
}

/* PI status polling: never busy. Writes go nowhere. */
unsigned int ref_io_read(unsigned int addr) { (void) addr; return 0; }
void ref_io_write(unsigned int addr, unsigned int value) { (void) addr; (void) value; }

/* binary32 square root as an opaque call (see the Makefile's -Dsqrtf=ref_sqrtf).
 * SSE sqrtss is correctly rounded binary32, as VR4300 sqrt.s is. glibc's i386 sqrtf is not
 * usable here: it returns the x87 fsqrt result in st(0) unrounded, and Clang widens that
 * straight to double while GCC rounds it to float, so the two disagreed by an ulp. */
float ref_sqrtf(float x)
{
    float r;
    __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x));
    return r;
}

/* menus.c: the race awards a life through this; its counter is HUD state. */
void func_i3_80128D8C(void) { ++ref_player_lives_increments; }
