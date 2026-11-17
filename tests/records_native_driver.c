#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "records_native.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);return 1;}}while(0)

/* This fixture records native API/adapter arguments. It does not emulate
 * SH-2 projection or VDP1 rendering; their separate gates remain necessary. */
static sh2z_view_t recorded_view;
static sh2z_pos16_t recorded_positions[4];
static unsigned matrix_fault,projection_fault;
bool fzx_records_machine_matrix(int32_t out[18]) {
    memset(out,0,18*sizeof(*out));out[0]=out[5]=out[10]=out[15]=out[16]=out[17]=65536;
    if(matrix_fault)out[0]++;
    return true;
}
bool fzx_records_spinner_matrix(const fzx_records_draw_snapshot_t *p,int32_t out[18]) {
    if(!p||!out||p->spinner.visible!=1)return false;
    fzx_records_machine_matrix(out);out[14]=-400*65536;
    if(projection_fault==1)out[3]=1;
    if(projection_fault==2)out[16]=INT32_MAX;
    return true;
}
bool sh2z_view_init(sh2z_view_t *out,const sh2z_view_t *in) {
    if(!out||!in)return false;
    *out=*in;recorded_view=*in;return true;
}
bool sh2z_view_batch_trunc_divu(sh2z_view_vertex_t *out,const sh2z_view_t *v,
                               const void *positions,size_t stride,size_t count) {
    if(count!=4||stride!=sizeof(sh2z_pos16_t))return false;
    memcpy(recorded_positions,positions,sizeof(recorded_positions));
    for(unsigned i=0;i<4;++i) {
        memset(out+i,0,sizeof(*out));
        out[i].sx=v->center_x+recorded_positions[i].x*2;
        out[i].sy=v->center_y-recorded_positions[i].y*2;
        if(projection_fault==3)out[i].outcode=SH2Z_OUT_NEAR;
    }
    return true;
}
static fzx_results_draw_t draw(unsigned image,unsigned kind) {
    return (fzx_results_draw_t){.x=20,.y=30,.x2=24,.y2=32,.sx=2,.sy=2,.w=4,.h=2,
        .clip_x=21,.clip_y=31,.clip_x2=320,.clip_y2=240,.image=(uint16_t)image,
        .kind=(uint8_t)kind,.r=255,.g=255,.b=255,.a=255};
}
int main(int argc,char **argv) {
    void *mapped=mmap((void *)0x25c00000u,0x80000u,PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(mapped!=MAP_FAILED);
    uint8_t *pixels=(uint8_t *)mapped+0x10000,*palettes=pixels+1024,*tints=palettes+6144,*previews=tints+64;
    struct {vdp1_cmdt_t c[FZX_RECORDS_COMMANDS];uint32_t guard;} commands;
    memset(&commands,0,sizeof(commands));commands.guard=0x84239abdu;
    uint16_t selected[6]={0};unsigned n=99,index=99,offset=99,bytes=99;
    fzx_records_draw_snapshot_t f={0};f.count=4;
    f.draws[0]=draw(3,FZX_RESULTS_IMAGE);f.draws[0].g=f.draws[0].b=102;
    f.draws[1]=draw(1,FZX_RESULTS_GLYPH);f.draws[1].r=128;
    f.draws[2]=draw(1,FZX_RESULTS_GLYPH);f.draws[2].b=0;
    f.draws[3]=draw(0,FZX_RESULTS_RECT);f.draws[3].a=128;
    if(argc>1&&strcmp(argv[1],"overlap")==0) {
        CHECK(!fzx_records_native(commands.c,8,&f,pixels,pixels,tints,6,previews,selected,&n)&&n==0);
        CHECK(!fzx_records_native(commands.c,8,&f,pixels,palettes,pixels,6,previews,selected,&n)&&n==0);
        CHECK(!fzx_records_native(commands.c,8,&f,pixels,palettes,tints,6,pixels,selected,&n)&&n==0);
        puts("RECORDS_NATIVE_OVERLAP_REJECTIONS_PASS");return 0;
    }
    if(argc>1&&strcmp(argv[1],"model-span")==0) {
        CHECK(!fzx_records_model(29,0x1d0301u,&index,&offset,&bytes));
        puts("RECORDS_NATIVE_MODEL_SPAN_REJECTION_PASS");return 0;
    }
    CHECK(fzx_records_native(commands.c,8,&f,pixels,palettes,tints,6,previews,selected,&n)&&n==8);
    CHECK(commands.guard==0x84239abdu);
    for(unsigned i=0;i<8;i+=2)CHECK(commands.c[i].kind==8&&commands.c[i].cmd_xa==21&&commands.c[i].cmd_ya==23&&commands.c[i].cmd_xc==23&&commands.c[i].cmd_yc==23);
    CHECK(commands.c[1].kind==0&&commands.c[1].texture==(uintptr_t)tints+32&&commands.c[1].width==8&&commands.c[1].height==2);
    CHECK(commands.c[1].cmd_xa==18&&commands.c[1].cmd_ya==22&&commands.c[1].mode.user_clipping_mode==2);
    CHECK(commands.c[1].mode.end_code_disable==1&&commands.c[1].mode.trans_pixel_disable==0&&commands.c[1].mode.color_mode==5);
    CHECK(commands.c[3].mode.color_mode==1&&commands.c[3].texture==(uintptr_t)pixels+72);
    CHECK(commands.c[3].cmd_colr==(((uintptr_t)palettes&0x7ffffu)>>3));
    CHECK(commands.c[5].cmd_colr==((((uintptr_t)palettes+32)&0x7ffffu)>>3));
    CHECK(((uint16_t *)palettes)[0]==0&&((uint16_t *)palettes)[1]==0xffefu);
    CHECK(((uint16_t *)palettes)[2]==0x8000u);
    CHECK(((uint16_t *)(palettes+32))[1]==0x83ffu);
    CHECK(commands.c[7].kind==4&&commands.c[7].mode.trans_pixel_disable==1&&commands.c[7].mode.cc_mode==3);
    CHECK(commands.c[7].cmd_xa==21&&commands.c[7].cmd_xb==23&&commands.c[7].cmd_ya==23&&commands.c[7].cmd_yd==23);
    CHECK(!fzx_records_native(commands.c,7,&f,pixels,palettes,tints,6,previews,selected,&n)&&n==0);
    CHECK(!fzx_records_native(commands.c,8,&f,pixels+1,palettes,tints,6,previews,selected,&n)&&n==0);
    CHECK(!fzx_records_native(commands.c,8,&f,pixels,palettes,tints,7,previews,selected,&n)&&n==0);
    CHECK(!fzx_records_native(commands.c,8,&f,pixels,palettes,tints,16,previews,selected,&n)&&n==0);
    CHECK(!fzx_records_native(commands.c,8,&f,pixels,palettes,tints,6,(uint8_t *)mapped+0x7fff8,selected,&n)&&n==0);
    f.draws[0].r=254;
    CHECK(!fzx_records_native(commands.c,8,&f,pixels,palettes,tints,6,previews,selected,&n)&&n==0);
    f=(fzx_records_draw_snapshot_t){0};f.count=192;
    for(unsigned i=0;i<193;++i)f.draws[i]=draw(1,FZX_RESULTS_GLYPH);
    *((uint32_t *)(palettes+6144))=0x9726eacbu;
    CHECK(fzx_records_native(commands.c,384,&f,pixels,palettes,tints,15,previews,selected,&n)&&n==384);
    CHECK(*((uint32_t *)(palettes+6144))==0x9726eacbu);
    f.count=193;
    CHECK(!fzx_records_native(commands.c,386,&f,pixels,palettes,tints,15,previews,selected,&n)&&n==0);
    CHECK(*((uint32_t *)(palettes+6144))==0x9726eacbu);
    CHECK(fzx_records_model(29,0x1d0101u,&index,&offset,&bytes)&&index==117&&offset==64+117*32&&bytes==32);
    CHECK(!fzx_records_model(30,0,&index,&offset,&bytes));
    CHECK(!fzx_records_model(29,0xabcdefu,&index,&offset,&bytes));
    CHECK(!fzx_records_model(29,0x1d0101u,NULL,&offset,&bytes));
    CHECK(fzx_records_model_camera());matrix_fault=1;CHECK(!fzx_records_model_camera());matrix_fault=0;
    f=(fzx_records_draw_snapshot_t){0};f.machine_count=2;
    f.machines[0]=(fzx_records_machine_draw_t){.slot=5,.character=29,.x=90,.y=70,.r=29,.g=1,.b=1};
    f.machines[1]=(fzx_records_machine_draw_t){.slot=1,.character=2,.x=120,.y=110,.r=2,.g=3,.b=1};
    selected[0]=117;selected[1]=11;
    CHECK(fzx_records_native(commands.c,3,&f,pixels,palettes,tints,15,previews,selected,&n)&&n==3);
    CHECK(commands.c[0].kind==8&&commands.c[0].cmd_xc==319&&commands.c[0].cmd_yc==223);
    CHECK(commands.c[1].texture==(uintptr_t)previews&&commands.c[2].texture==(uintptr_t)previews+32);
    CHECK(commands.c[1].cmd_xa==88&&commands.c[1].cmd_ya==61&&commands.c[2].cmd_xa==118&&commands.c[2].cmd_ya==101);
    CHECK(commands.c[1].width==8&&commands.c[1].height==2&&commands.c[1].mode.color_mode==5);
    CHECK(!fzx_records_native(commands.c,2,&f,pixels,palettes,tints,15,previews,selected,&n)&&n==0);
    selected[1]=120;CHECK(!fzx_records_native(commands.c,3,&f,pixels,palettes,tints,15,previews,selected,&n)&&n==0);
    f=(fzx_records_draw_snapshot_t){0};f.spinner=(fzx_records_spinner_draw_t){.visible=1,.image=8,.x=150,.y=80,.width=8,.height=16};
    CHECK(fzx_records_native(commands.c,2,&f,pixels,palettes,tints,15,previews,selected,&n)&&n==2);
    CHECK(recorded_view.center_x==150&&recorded_view.center_y==72&&recorded_view.focal_x==65536*320&&recorded_view.focal_y==65536*240);
    CHECK(recorded_view.r[0][0]==16384&&recorded_view.r[1][1]==16384&&recorded_view.r[2][2]==-16384&&recorded_view.t[2]==400*16384);
    CHECK(recorded_view.near_z==16*16384&&recorded_view.far_z==8129*16384&&recorded_view.clip[3]==223);
    CHECK(recorded_positions[0].x==-4&&recorded_positions[0].y==8&&recorded_positions[3].x==4&&recorded_positions[3].y==-8);
    CHECK(commands.c[1].kind==2&&commands.c[1].texture==(uintptr_t)pixels+512&&commands.c[1].width==8&&commands.c[1].height==16);
    CHECK(commands.c[1].cmd_xa==142&&commands.c[1].cmd_ya==56&&commands.c[1].cmd_xb==158&&commands.c[1].cmd_yb==56);
    CHECK(commands.c[1].cmd_xc==158&&commands.c[1].cmd_yc==88&&commands.c[1].cmd_xd==142&&commands.c[1].cmd_yd==88);
    for(projection_fault=1;projection_fault<=3;++projection_fault)
        CHECK(!fzx_records_native(commands.c,2,&f,pixels,palettes,tints,15,previews,selected,&n)&&n==0);
    projection_fault=0;f.spinner.image=1;f.spinner.height=4;
    f.count=1;f.draws[0]=draw(1,FZX_RESULTS_GLYPH);f.draws[0].r=0;
    CHECK(fzx_records_native(commands.c,4,&f,pixels,palettes,tints,15,previews,selected,&n)&&n==4);
    CHECK(commands.c[3].mode.color_mode==1&&commands.c[3].texture==(uintptr_t)pixels+64);
    CHECK(commands.c[3].cmd_colr==(((uintptr_t)palettes+32)&0x7ffffu)/8);
    CHECK(((uint16_t *)(palettes+32))[0]==0&&((uint16_t *)(palettes+32))[1]==65535);
    f.count=192;for(unsigned i=0;i<f.count;++i)f.draws[i]=draw(1,FZX_RESULTS_GLYPH);
    CHECK(!fzx_records_native(commands.c,386,&f,pixels,palettes,tints,15,previews,selected,&n)&&n==0);
    CHECK(*((uint32_t *)(palettes+6144))==0x9726eacbu);
    CHECK(commands.guard==0x84239abdu);
    puts("RECORDS_NATIVE_API_AND_CONTROLS_PASS");return 0;
}
