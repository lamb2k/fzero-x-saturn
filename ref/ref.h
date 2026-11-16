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

/* Where racer 0 is drawn after a tick (not traced): Racer.modelPos and Racer.modelBasis, rows
 * x, y, z of the original's Mtx3F (forward, up, left), plus what a HUD shows. */
typedef struct {
    float pos[3];
    float basis[3][3];
    float speed, energy;
    int intro, lap, laps_completed, race_time, state_flags;
} RefPose;

/* Read-only source timing for the native Time Attack HUD. RefPose/trace ABI
 * remains unchanged; lap times include the source crossing interpolation. */
typedef struct {
    int lap, race_time, lap_times[3];
} RefHud;

#ifndef FZX_SOURCE_CAMERA
#define FZX_SOURCE_CAMERA 0
#endif
#if FZX_SOURCE_CAMERA
/* Source camera, after Camera_Update. Source game units and row-vector matrices;
 * native presentation must explicitly adapt its viewport and clipping. */
typedef struct {
    int mode, setting, timer, look_back;
    float eye[3], at[3], basis[3][3];
    float fov, near_plane, far_plane;
    float viewport[4], scissor[4]; /* scale X/Y, translate X/Y; L/T/R/B */
    float view[4][4], projection[4][4];
    unsigned int faults;
} RefCamera;
void ref_camera_init(int cold);
void ref_camera_tick(void);
void ref_camera(RefCamera* out);
unsigned int ref_camera_count(void);
int ref_camera_read(unsigned int index, RefCamera* out);
#endif

/* Bridge side. */
int ref_load_course(const unsigned char* record, int big_endian); /* 0 on success */
void ref_set_rom_boot(const unsigned char* boot); /* big-endian (z64) bytes; NULL = CI fixture */
int ref_race_init(void);                                    /* 0 on success */
/* Back to the grid on the course already set up: the racer steps of Race_Init again, without
 * Course_Init (seconds on the SH-2). check.sh proves a restarted race replays as a fresh one. */
void ref_race_restart(void);
/* Hand racer 0 to the original CPU driver (RACER_STATE_CPU_CONTROLLED, as the title demo does) or
 * back to the pad. Whole-race test driver: stand-in sizing and long STRICT runs. */
void ref_set_cpu_driver(int on);
void ref_tick(unsigned short buttons, signed char stick_x, signed char stick_y);
void ref_state(RefState* out);
void ref_pose(RefPose* out);
void ref_hud(RefHud* out);

/* Shared between bridge.c and hooks.c. */
extern int ref_course_ready;
extern int ref_course_loads;
extern unsigned char ref_course_bytes[REF_COURSE_RECORD_SIZE];
extern unsigned short ref_pad_button;
extern signed char ref_pad_stick_x, ref_pad_stick_y;

#endif
