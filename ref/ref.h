/* Reference harness interface between the host driver (main.c, host headers
 * only) and the bridge (bridge.c and hooks.c, decomp headers only).
 * Plain C types only, so both sides compile it. MIT; see ../LICENSE. */
#ifndef FZX_REF_H
#define FZX_REF_H

#define REF_COURSE_RECORD_SIZE 0x7E0
/* ROM_READ only touches the boot area (Racer_InitMachineStats reads its machine constants
 * from offsets below 0x1000), so that is all the harness keeps. */
#define REF_ROM_BOOT_SIZE 0x1000

/* The traced state of racer 0 after a tick. Field order is the trace order. */
typedef struct {
    int intro;
    float pos[3], vel[3], speed, energy, lap_distance, seg_t, height;
    int segment, lap, laps_completed, race_time, state_flags, boost_timer;
} RefState;

/* Bridge side. */
int ref_load_course(const unsigned char* record, int big_endian); /* 0 on success */
void ref_set_rom_boot(const unsigned char* boot); /* big-endian (z64) bytes; NULL = CI fixture */
int ref_race_init(void);                                    /* 0 on success */
void ref_tick(unsigned short buttons, signed char stick_x, signed char stick_y);
void ref_state(RefState* out);

/* Shared between bridge.c and hooks.c. */
extern int ref_course_ready;
extern int ref_course_loads;
extern unsigned char ref_course_bytes[REF_COURSE_RECORD_SIZE];
extern unsigned short ref_pad_button;
extern signed char ref_pad_stick_x, ref_pad_stick_y;

#endif
