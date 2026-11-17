#define _GNU_SOURCE
#include "countdown_scene.h"
#include "texture_cd.h"
#include "countdown_identity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
unsigned char scratch_storage[32768] __attribute__((aligned(32)));
__asm__(".global sat_stub_data_end\n.set sat_stub_data_end,scratch_storage\n.global fzx_input_log\n.set fzx_input_log,scratch_storage+32768\n");
static int fault,loads,timer=460,reads;
static uint32_t scratch_need=1024;
static uint16_t clock_tick;
uint16_t cpu_frt_count_get(void){return ++clock_tick;}
uint32_t fzx_texture_cd_scratch_bytes(void){return scratch_need;}
bool fzx_texture_cd_load(const char *name,uint32_t bytes,uint32_t hash,void *out,void *scratch,uint32_t available){
 assert(scratch==scratch_storage&&available==32736);++loads;
 if(loads==1){assert(!strcmp(name,"COUNTDN.PAK")&&bytes==2352&&hash==FZX_COUNTDOWN_HASH);memset(out,0x11,bytes);}
 else {assert(loads==2&&!strcmp(name,"COUNTDTX.PAK")&&bytes==70400&&hash==FZX_COUNTDOWN_TEXTURE_HASH);memset(out,0x22,bytes);}
 if(fault==6)((volatile uint32_t *)(uintptr_t)fzx_countdown_low_end)[0]=0;
 return !(fault==1&&loads==1)&&!(fault==3&&loads==2);
}
bool fzx_countdown_pack_open(fzx_countdown_pack_t *p,const void *data,uint32_t bytes,uint32_t hash,uint32_t texture_bytes,uint32_t texture_hash){
 assert(bytes==2352&&hash==FZX_COUNTDOWN_HASH&&texture_bytes==70400&&texture_hash==FZX_COUNTDOWN_TEXTURE_HASH);
 assert((uintptr_t)p>=fzx_countdown_low_start+2352&&(uintptr_t)p+sizeof(*p)<=fzx_countdown_low_end);
 assert(((const unsigned char *)data)[2351]==0x11);p->data=data;p->bytes=bytes;p->texture_bytes=texture_bytes;return fault!=2;
}
bool fzx_countdown_draw_native_prepare(fzx_countdown_native_work_t *w,const fzx_countdown_pack_t *p){
 assert((uintptr_t)w>=fzx_countdown_low_start+2352&&(uintptr_t)w+sizeof(*w)<=fzx_countdown_low_end);
 w->draw.pack=p;w->draw.prepared=true;return fault!=4;
}
int ref_countdown(RefCountdown *p){++reads;memset(p,0,sizeof(*p));p->timer=timer;p->faults=fault==5;return !p->faults;}
bool fzx_countdown_phase(int t,fzx_countdown_phase_t *p){*p=(fzx_countdown_phase_t){.visible=t>0};return t>=0&&t<=460;}
bool fzx_countdown_draw_native(fzx_countdown_native_work_t *w,const fzx_view_t *v,const RefCountdown *s,const fzx_countdown_phase_t *p,void *bank,uint32_t bytes,vdp1_cmdt_t *out,uint32_t capacity,uint32_t *count,fzx_countdown_native_stats_t *stats){
 assert(w->draw.prepared&&v&&s->timer==timer&&p->visible==(timer>0)&&bank==(void *)0x25c00000&&bytes==70400);
 *count=0;if(fault==7||capacity<2){stats->fault=FZX_COUNTDOWN_NATIVE_CAPACITY;return false;}
 if(fault==8)((volatile uint32_t *)(uintptr_t)fzx_countdown_low_end)[1]=0;
 if(p->visible){memset(out,0x33,2*sizeof(*out));*count=2;stats->commands=2;}return true;
}
static void reset(void){fault=0;loads=0;timer=460;scratch_need=1024;memset((void *)0x200000,0xdd,0x100000);}
static bool load(void){loads=0;return fzx_countdown_load(0x210000,(void *)0x25c00000,70400);}
int main(void){
 assert(mmap((void *)0x200000,0x100000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)0x200000);
 assert(mmap((void *)0x25c00000,0x80000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)0x25c00000);
 vdp1_cmdt_t out[2];fzx_view_t view={0};uint32_t count=99;
 reset();assert(!fzx_countdown_guards());assert(!fzx_countdown_native(out,2,&view,&count)&&count==0);
 const uint32_t ends[]={0,0x1fffff,0x2fffa1,0xffffffff,0x2ff000};
 for(unsigned i=0;i<sizeof(ends)/sizeof(ends[0]);++i){assert(!fzx_countdown_load(ends[i],(void *)0x25c00000,70400));assert(fzx_countdown_status==2&&!loads&&!fzx_countdown_busy);}
 assert(!fzx_countdown_load(0x210000,(void *)0x25c00001,70400));assert(!loads);
 assert(!fzx_countdown_load(0x210000,(void *)0x25c00000,70399));assert(!loads);
 assert(!fzx_countdown_load(0x210000,(void *)0x25c7fff8,70400));assert(!loads);
 scratch_need=32737;assert(!load()&&fzx_countdown_status==2&&!loads);scratch_need=1024;
 for(int f=1;f<=6;++f){reset();fault=f;if(f==5)continue;assert(!load());assert(fzx_countdown_status==(uint32_t)(f==6?7:f+2));assert(!fzx_countdown_busy);}
 reset();assert(load());assert(loads==2&&fzx_countdown_status==1&&fzx_countdown_guards());
 assert(fzx_countdown_low_start==0x210040&&fzx_countdown_low_end<0x220000);assert(*(volatile uint32_t *)0x210000==0xdddddddd);
 assert(fzx_countdown_texture_start==0x25c00000&&fzx_countdown_texture_bytes==70400);
 assert(!fzx_countdown_native(out,2,&view,&count)&&count==0&&fzx_countdown_status==8);
 reset();assert(load());assert(fzx_countdown_publish()&&reads==1);assert(fzx_countdown_native(out,2,&view,&count));assert(count==2&&fzx_countdown_stats.commands==2&&fzx_countdown_ticks==1&&!fzx_countdown_busy);
 timer=0;assert(fzx_countdown_publish());memset(out,0x44,sizeof(out));assert(fzx_countdown_native(out,2,&view,&count)&&!count&&!fzx_countdown_command_start&&((unsigned char *)out)[0]==0x44);
 fault=5;assert(!fzx_countdown_publish()&&fzx_countdown_status==8&&!fzx_countdown_busy);
 reset();assert(load()&&fzx_countdown_publish());fault=7;assert(!fzx_countdown_native(out,2,&view,&count)&&!count&&fzx_countdown_status==9&&fzx_countdown_stats.fault==FZX_COUNTDOWN_NATIVE_CAPACITY);
 reset();assert(load()&&fzx_countdown_publish());fault=8;assert(!fzx_countdown_native(out,2,&view,&count)&&!count&&fzx_countdown_status==7&&!fzx_countdown_busy);
 reset();assert(load());((volatile uint32_t *)(uintptr_t)(fzx_countdown_low_start-32))[7]=0;assert(!fzx_countdown_publish()&&fzx_countdown_status==7);
 reset();assert(load());assert(!fzx_countdown_native(NULL,2,&view,&count)&&fzx_countdown_status==10&&!count);
 printf("scene HOST mock checks PASS; sizeof native work=%zu pack=%zu; guards/load/source publication/draw failure controls\n",sizeof(fzx_countdown_native_work_t),sizeof(fzx_countdown_pack_t));return 0;
}
