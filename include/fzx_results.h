#ifndef FZX_RESULTS_H
#define FZX_RESULTS_H
#include <stdbool.h>
#if defined(FZX_RESULTS_SOURCE_ABI)
/* Original source's libc/stdint.h supplies pointer types only. Its SH-2
 * closure uses these compiler-width identities; native/host code uses libc. */
typedef __INT16_TYPE__ int16_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT8_TYPE__ uint8_t;
#else
#include <stdint.h>
#endif

#define FZX_RESULTS_DRAW_MAX 192u
#define FZX_RESULTS_EVENT_MAX 16u
#define FZX_RESULTS_NAME_MAX 64u

enum { FZX_RESULTS_RACING, FZX_RESULTS_FINISH, FZX_RESULTS_TIMES,
       FZX_RESULTS_EXITING, FZX_RESULTS_RECORDS_REQUEST };
enum { FZX_RESULTS_IMAGE, FZX_RESULTS_GLYPH, FZX_RESULTS_RECT };
enum { FZX_RESULTS_SOUND_SYSTEM, FZX_RESULTS_SOUND_BGM,
       FZX_RESULTS_SOUND_LEVEL_STOP, FZX_RESULTS_SAVE_REQUEST };
enum { FZX_RESULTS_HUD_TIME, FZX_RESULTS_HUD_ENERGY, FZX_RESULTS_LAP_SYMBOLS,
       FZX_RESULTS_SPEED_DIGITS, FZX_RESULTS_KMH, FZX_RESULTS_TIMER_SYMBOLS,
       FZX_RESULTS_LAP, FZX_RESULTS_MAX_SPEED };

/* Source 320x240 coordinates; consumers crop y by eight for Saturn. Every
 * destination and scissor uses half-open edges. IMAGE crops are texels in
 * sheets 0..7 above; GLYPH image is the original Font3 table index, resolved
 * by the generated asset identity. No source text or texture data lives here. */
typedef struct {
    int16_t x,y,x2,y2,sx,sy,w,h;
    int16_t clip_x,clip_y,clip_x2,clip_y2;
    uint16_t image;
    uint8_t kind,r,g,b,a;
} fzx_results_draw_t;
typedef struct { uint16_t id; uint8_t kind,player; } fzx_results_event_t;
typedef struct {
    float x,y,z,scale,rotation,rotation_speed;
    int32_t angle,timer;
    uint8_t visible;
} fzx_results_finish_t;

/* Original MachineInfo fields, carried independently of native model IDs. */
typedef struct {
    uint8_t character,customType,frontType,rearType,wingType,logo,number,decal;
    uint8_t bodyR,bodyG,bodyB,numberR,numberG,numberB,decalR,decalG,decalB;
    uint8_t cockpitR,cockpitG,cockpitB;
} fzx_results_machine_t;
typedef struct {
    int32_t timeRecord[5],bestTime;
    float recordEngines[5],maxSpeed;
    char recordNames[5][4];
    fzx_results_machine_t recordMachineInfos[5],maxSpeedMachine,bestTimeMachine;
    int32_t machine_indices[7];
} fzx_results_records_t;

/* Only original three-lap, single-player Time Attack without a staff ghost.
 * config.name is source course text supplied by the private source bridge.
 * reset(NULL records) uses source fresh-save defaults. Provided records are
 * in-memory session state; this API never claims a durable save. */
typedef struct { char name[FZX_RESULTS_NAME_MAX]; uint8_t course; } fzx_results_config_t;
typedef struct {
    uint32_t frame,state_flags;
    int32_t camera_mode,lap_times[3],race_time,machine_index;
    float speed,max_speed,engine;
    fzx_results_machine_t machine;
    uint16_t button_pressed;
    uint8_t record_eligible,connected;
} fzx_results_input_t;

typedef struct {
    fzx_results_draw_t draws[FZX_RESULTS_DRAW_MAX];
    fzx_results_event_t events[FZX_RESULTS_EVENT_MAX];
    fzx_results_finish_t finish;
    uint32_t tick,frame;
    int32_t finish_ticks,results_ticks,outgoing;
    uint16_t count;
    uint8_t phase,event_count,record_position,best_lap,max_speed_record;
    uint8_t name_entry_pending,save_pending,ghost_check_pending,records_pending;
} fzx_results_snapshot_t;

/* Public storage, not a mutation API. Use reset/step; snapshot is a pure copy.
 * Prior input is retained because source Menus_Update precedes Racer_Update;
 * a queued finish-save event must observe the previous source racer state. */
typedef struct {
    fzx_results_config_t config;
    fzx_results_input_t input;
    fzx_results_records_t records;
    fzx_results_snapshot_t snapshot;
    uint32_t ticks,save_requests;
    int32_t finish_timer[1],finish_state[1],finish_initialized[1];
    int32_t special_timer[1],finish_gameover_timer[1],loser_timer[1],results_timer[1];
    int32_t results_started[1],finished_success_time,outgoing,menu_flags,finish_saved;
    int32_t record_position,best_lap,max_speed_record,name_entry;
    float rotation[1],rotation_speed[1];
    int32_t layout_left,layout_top,layout_spacing,layout_lap_offset;
    int32_t layout_start,layout_start_spacing,layout_speed;
    uint8_t initialized,phase,save_pending,ghost_check_pending,records_pending,fault;
} fzx_results_state_t;

bool fzx_results_reset(fzx_results_state_t *state,const fzx_results_config_t *config,
                       const fzx_results_records_t *records);
/* Original first-course name, generated privately from the source table. */
bool fzx_results_default_config(fzx_results_config_t *config);
/* Call exactly once after each original Racer_Update + Camera_Update.
 * source buttonPressed is supplied directly; held-button repeat is not a
 * results adapter policy. Terminal RECORDS_REQUEST never redirects to retry.
 * Events belong to snapshot.tick and must be consumed at most once per tick. */
bool fzx_results_step(fzx_results_state_t *state,const fzx_results_input_t *input);
bool fzx_results_snapshot(const fzx_results_state_t *state,fzx_results_snapshot_t *out);
/* Optional source-math bridge: pure model*fixed-FINISH-camera row-vector
 * Q16.16 matrix[16] followed by projection XX/YY. The original sine table
 * must already be initialized. Hidden/invalid FINISH returns false unchanged.
 * Link generated results_matrix_bridge.c with the original source math. */
bool fzx_results_finish_matrix(const fzx_results_snapshot_t *snapshot,int32_t out[18]);
#endif
