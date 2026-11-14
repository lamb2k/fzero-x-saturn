/* Float reference harness (card G1.2). MIT; see ../LICENSE.
 * Shared by the game side (decomp headers) and the libc side, so plain C types only:
 * no <stdint.h>, whose typedefs clash with the decomp's. */
#ifndef REF_H
#define REF_H
#define REF_COURSE_RECORD_SIZE 0x7E0
#define REF_TRACE_FORMAT 1
#define REF_AUTO_STEER 99 /* script stick_x meaning "bot steers" */
#define REF_RACER_BYTES_MAX 0x400
#define REF_ROM_HEAD_BYTES 0x1000
/* Fixture machine tuning: an authored constant for every ROM word Racer_InitMachineStats
 * reads. Not retail values; retail tuning needs --rom (the ROM lane). */
#define REF_FIXTURE_ROM_WORD 0x20202020u

typedef struct {
    unsigned char racer[REF_RACER_BYTES_MAX]; /* the whole Racer, pointers replaced */
    unsigned int racer_bytes;
    unsigned int state_flags;
    int segment;                     /* index of the racer's course segment, -1 if none */
    float t, pos[3], vel[3], speed, energy, race_distance;
    float seg_forward[3], seg_disp[3];   /* spline tangent and offset from the spline point */
    float up[3];                          /* racer's up axis (trueBasis.y) */
    int lap, laps_completed;
} RefSnapshot;

/* The course record in host byte order, served by the DMA shim to Course_Load. */
extern unsigned char ref_course_record[REF_COURSE_RECORD_SIZE];
extern unsigned int ref_dma_course_loads, ref_dma_other_loads;
extern int ref_player_lives_increments;
extern unsigned char ref_rom_head[REF_ROM_HEAD_BYTES];
extern int ref_rom_loaded;
extern unsigned int ref_fixture_rom_word;
extern unsigned int ref_rom_reads;

void ref_game_init(int machine);
void ref_game_tick(unsigned short buttons, signed char stick_x, signed char stick_y);
void ref_game_snapshot(RefSnapshot* out);
int ref_game_course_values(float* tension, float* length, int max);
#endif
