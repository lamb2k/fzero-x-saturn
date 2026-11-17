/* Offline stock-US course preview oracle. Retail data stays private.
 * Compile once with decomp headers, once with FZX_PREVIEW_DRIVER for host I/O. */
#ifdef FZX_PREVIEW_DRIVER
#include <stdio.h>
#include <stdlib.h>
#include "ref.h"
extern int preview_course(const unsigned char *,int);
extern int preview_cameras(void);
extern void ref_stub_call_report(const char *);
void preview_vertex(int index,const short *xyz) {
    printf("V %d %d %d %d\n",index,xyz[0],xyz[1],xyz[2]);
}
void preview_count(int course,int count) { printf("C %d %d\n",course,count); }
void preview_camera(int index,const RefCamera *c) {
    const float *values=&c->view[0][0];
    int i;
    printf("M %d",index);
    for(i=0;i<16;i++) printf(" %a",(double)values[i]);
    printf(" %a %a\n",(double)c->projection[0][0],(double)c->projection[1][1]);
}
int main(int argc,char **argv) {
    unsigned char record[REF_COURSE_RECORD_SIZE];
    FILE *f;
    int i,status=0;
    if(argc!=3 || !(f=fopen(argv[1],"rb"))) return 2;
    puts("FZXCPVRAW1");
    for(i=0;i<24;i++) {
        if(fread(record,1,sizeof(record),f)!=sizeof(record)) { status=3;break; }
        status=preview_course(record,i);
        if(status) break;
    }
    if(!status && fgetc(f)!=EOF) status=4;
    fclose(f);
    if(!status) status=preview_cameras();
    ref_stub_call_report(argv[2]);
    return status;
}
#else
#include "global.h"
#include "fzx_course.h"
#include "fzx_game.h"
#include "fzx_camera.h"
#include "ref.h"
extern s32 gGameMode,gNumPlayers,D_i5_801190D0;
extern CourseInfo gCourseInfos[];
extern OSPiHandle *gCartRomHandle;
extern GfxPool *gGfxPool;
extern void preview_vertex(int,const short *);
extern void preview_count(int,int);
extern void preview_camera(int,const RefCamera *);
static GfxPool preview_pool;
static OSPiHandle preview_cart;
static struct { u32 before[16]; Vtx vertices[0xC00]; u32 after[16]; } storage;
static void boot(void) {
    gCartRomHandle=&preview_cart;
    gGfxPool=&preview_pool;
    gGameMode=GAMEMODE_FLX_COURSE_SELECT;
    gNumPlayers=1;
    Math_SinTableInit();
}
int preview_course(const unsigned char *record,int index) {
    int i,count;
    if(index<0 || index>=24) return 10;
    boot();
    if(ref_load_course(record,1)) return 11;
    ref_course_loads=0;
    Course_Load(index);
    if(ref_course_loads!=1) return 12;
    func_80074634(&gCourseInfos[index]);
    Course_SegmentLengthsInit(&gCourseInfos[index]);
    memset(&storage,0xA5,sizeof(storage));
    count=func_800A2D2C(&gCourseInfos[index],storage.vertices);
    if(count<6 || count>=0xC00 || count%3) return 13;
    for(i=0;i<16;i++) if(storage.before[i]!=0xA5A5A5A5 || storage.after[i]!=0xA5A5A5A5) return 14;
    for(i=0;i<3;i++) if(memcmp(storage.vertices[i].v.ob,storage.vertices[count-3+i].v.ob,6)) return 15;
    preview_count(index,count);
    /* Only XYZ are initialized by func_800A2BDC; never export Vtx padding/UV/color. */
    for(i=0;i<count;i++) preview_vertex(i,storage.vertices[i].v.ob);
    return 0;
}
int preview_cameras(void) {
    int i;
    RefCamera camera;
    boot();
    D_i5_801190D0=0;
    ref_camera_init(1);
    for(i=0;i<361;i++) {
        ref_camera_tick();
        if(!ref_camera_read(0,&camera) || camera.faults) return 20;
        preview_camera(i,&camera);
        D_i5_801190D0=1;
    }
    return 0;
}
#endif
