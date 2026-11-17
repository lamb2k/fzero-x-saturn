/* Offline source-camera capture. Compile bridge with decomp headers and driver
 * with host headers. All matrices come from original Camera_Init/Update. */
#ifdef FZX_MACHINE_CAMERA_DRIVER
#include <stdio.h>
#include <string.h>
#include "ref.h"
extern int machine_menu_cameras(void);
extern void ref_stub_call_report(const char *);
void machine_camera_packet(int run,int scene,int frame,const RefCamera *camera) {
    unsigned int words[sizeof(*camera)/sizeof(unsigned int)],i;
    memcpy(words,camera,sizeof(words));
    printf("R %d %d %d",run,scene,frame);
    for(i=0;i<sizeof(words)/sizeof(words[0]);++i)printf(" %08x",words[i]);
    putchar('\n');
}
int main(int argc,char **argv) {
    int status;
    if(argc!=2)return 2;
    puts("FZXMCRAW1");
    status=machine_menu_cameras();
    ref_stub_call_report(argv[1]);
    return status;
}
#else
#include "global.h"
#include "fzx_camera.h"
#include "fzx_game.h"
#include "ref.h"
extern s32 gGameMode,gNumPlayers;
extern GfxPool *gGfxPool;
extern void machine_camera_packet(int,int,int,const RefCamera *);
static GfxPool camera_pool;
int machine_menu_cameras(void) {
    int run,frame,scene;
    RefCamera camera;
    const int scenes[4]={0,1,1,0};
    gGfxPool=&camera_pool;gNumPlayers=1;
    Math_SinTableInit();
    for(run=0;run<4;++run) {
        scene=scenes[run];
        gGameMode=scene?GAMEMODE_LX_MACHINE_SETTINGS:GAMEMODE_FLX_MACHINE_SELECT;
        /* Cold select -> warm settings, cold settings -> warm select. */
        ref_camera_init(run==0 || run==2);
        if(ref_camera_count()!=1)return 10;
        for(frame=0;frame<8;++frame) {
            ref_camera_tick();
            if(!ref_camera_read(0,&camera) || camera.faults)return 11;
            machine_camera_packet(run,scene,frame,&camera);
        }
    }
    return 0;
}
#endif
