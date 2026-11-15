/* Reference harness hooks: the few N64 services the race code really uses.
 * MIT; see ../LICENSE. Everything else the decomp calls is a generated stub
 * (build/stubs.c) that does nothing and returns zero.
 *
 * Compiled with the decomp's own headers, so a prototype mismatch is a build
 * error, not a silent ABI bug. */
#include "global.h"
#include "fzx_course.h"
#include "ref.h"

/* The course is the one ROM read that matters. The original loader DMAs one
 * 0x7E0-byte CourseDataDisk record into the course context in 1 KB chunks,
 * through the decomp's own Dma_LoadAssetsAsync and Dma_RomCopyAsync, down to
 * this libultra call. Every chunk that lands in the course context gets the
 * matching slice of the harness's record. Any other ROM read (textures,
 * models, sound) is presentation and is skipped: its destination is often an
 * N64 segment address that does not exist here. */
s32 osEPiStartDma(OSPiHandle* handle, OSIoMesg* mb, s32 direction) {
    u8* base = (u8*) &COURSE_CONTEXT()->courseData;
    u8* dst = (u8*) (uintptr_t) mb->dramAddr;
    size_t size = mb->size;

    (void) handle;
    (void) direction;
    if (ref_course_ready && dst >= base && dst + size <= base + sizeof(CourseDataDisk)) {
        memcpy(dst, ref_course_bytes + (dst - base), size);
        if (dst == base) {
            ref_course_loads++;
        }
    }
    return 0;
}

void* osPhysicalToVirtual(u32 addr) {
    return (void*) (uintptr_t) addr;
}

/* ROM_READ (ref/overrides.h). The owner's ROM boot area when given, else the CI
 * fixture: an arbitrary fixed byte pattern, so the fixture machine's constants are
 * deterministic but carry no meaning. Real handling needs the real ROM. */
static unsigned char sRomBoot[REF_ROM_BOOT_SIZE];
static int sRomBootSet;

void ref_set_rom_boot(const unsigned char* boot) {
    s32 i;

    for (i = 0; i < REF_ROM_BOOT_SIZE; i++) {
        sRomBoot[i] = boot ? boot[i] : (unsigned char) ((i * 151 + 77) & 0xFF);
    }
    sRomBootSet = 1;
}

unsigned int ref_rom_read32(unsigned int offset) {
    offset &= 0x0FFFFFFF; /* Accept a raw offset or a cartridge address. */
    if (!sRomBootSet) {
        ref_set_rom_boot(NULL);
    }
    if (offset + 4 > REF_ROM_BOOT_SIZE) {
        return 0;
    }
    return ((unsigned int) sRomBoot[offset] << 24) | ((unsigned int) sRomBoot[offset + 1] << 16) |
           ((unsigned int) sRomBoot[offset + 2] << 8) | sRomBoot[offset + 3];
}

/* Identity mapping: the stock path passes a "physical" address as the DMA destination. */
u32 osVirtualToPhysical(void* addr) {
    return (u32) (uintptr_t) addr;
}

/* Pad 0 comes from the input script; the others are unplugged. */
void osContGetReadData(OSContPad* pads) {
    s32 i;

    for (i = 0; i < MAXCONTROLLERS; i++) {
        pads[i].button = 0;
        pads[i].stick_x = pads[i].stick_y = 0;
        pads[i].errno = (i == 0) ? 0 : CONT_NO_RESPONSE_ERROR;
    }
    pads[0].button = ref_pad_button;
    pads[0].stick_x = ref_pad_stick_x;
    pads[0].stick_y = ref_pad_stick_y;
}

s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    (void) mq;
    (void) flag;
    if (msg != NULL) {
        *msg = NULL;
    }
    return 0;
}
