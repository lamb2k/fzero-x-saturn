/* Read-only source snapshot for the separately extracted results state. */
#include "global.h"
#include "fzx_game.h"
#include "fzx_camera.h"
#include "fzx_racer.h"
#include "controller.h"
#include "fzx_results_bridge.h"

extern Racer gRacers[];
extern s32 gNumPlayers,gGameMode,gTotalLapCount,gCourseIndex;
extern s16 D_800F5DE8;
extern s32 gPlayerControlPorts[];
extern f32 gPlayerEngine[];
extern void func_80089A74(MachineInfo *);
_Static_assert(sizeof(MachineInfo)==sizeof(fzx_results_machine_t),"source machine record ABI");

bool ref_results_input(fzx_results_input_t *out,uint32_t frame,bool connected) {
    if(!out || gNumPlayers!=1 || gGameMode!=GAMEMODE_TIME_ATTACK ||
       gTotalLapCount!=3 || gCourseIndex!=0 || gPlayerControlPorts[0]<0 ||
       gPlayerControlPorts[0]>=4)return false;
    const Racer *r=&gRacers[0];
    fzx_results_input_t next={0};MachineInfo machine;
    func_80089A74(&machine);
    next.frame=frame;next.state_flags=(uint32_t)r->stateFlags;
    next.camera_mode=gCameras[0].mode;next.race_time=r->raceTime;
    next.machine_index=r->machineIndex;next.speed=r->speed;next.max_speed=r->maxSpeed;
    next.engine=gPlayerEngine[0];memcpy(next.lap_times,r->lapTimes,sizeof(next.lap_times));
    memcpy(&next.machine,&machine,sizeof(machine));
    next.connected=connected;next.record_eligible=D_800F5DE8!=0;
    next.button_pressed=connected?gControllers[gPlayerControlPorts[0]].buttonPressed:0;
    *out=next;return true;
}
