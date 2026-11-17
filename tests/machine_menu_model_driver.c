#include "fzx_machine_menu_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t hash(const uint8_t *p,size_t n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static uint8_t *readfile(const char *path,uint32_t *bytes) {
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    const long n=ftell(f);if(n<0||n>524288){fclose(f);return NULL;}
    rewind(f);uint8_t *p=malloc((size_t)n+1);
    if(!p||fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
    fclose(f);*bytes=(uint32_t)n;return p;
}
static uint32_t digest=2166136261u,kind_count[4];
static bool emit(void *context,const fzx_mm_quad_t *q) {
    (void)context;
    if(q->kind>3||q->character>=30)return false;
    ++kind_count[q->kind];
    for(unsigned i=0;i<4;++i)if(q->x[i]<-1024||q->x[i]>1023||q->y[i]<-1024||q->y[i]>1023)return false;
    const uint8_t *p=(const uint8_t *)q;for(unsigned i=0;i<sizeof(*q);++i)digest=(digest^p[i])*16777619u;
    return true;
}
static bool reject(void *context,const fzx_mm_quad_t *q){(void)context;(void)q;return false;}
int main(int argc,char **argv) {
    if(argc!=9)return 2;
    uint32_t nb,nc,nv,ns,nr;
    uint8_t *b=readfile(argv[1],&nb),*c=readfile(argv[2],&nc),*v=readfile(argv[3],&nv),
        *slots=readfile(argv[4],&ns),*reflection=readfile(argv[5],&nr);
    if(!b||!c||!v||!slots||!reflection||nv!=144||ns!=30||nr!=512)return 2;
    const unsigned screen=(unsigned)strtoul(argv[6],NULL,10),character=(unsigned)strtoul(argv[7],NULL,10),skin=(unsigned)strtoul(argv[8],NULL,10);
    fzx_mm_pack_t pack={0};fzx_mm_charts_t charts={0};
    if(!fzx_mm_open(&pack,b,nb,hash(b,nb))||!fzx_mm_charts_open(&charts,c,nc,hash(c,nc),&pack,screen,character,skin))return 1;
    if(fzx_mm_open(&pack,b,nb,hash(b,nb)^1u)||fzx_mm_charts_open(&charts,c,nc,hash(c,nc)^1u,&pack,screen,character,skin))return 3;
    int32_t cameras[2][18];
    for(unsigned i=0;i<36;++i){const uint8_t *p=v+i*4;uint32_t n=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
        cameras[i/18][i%18]=n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);}
    struct {uint32_t pre[8];fzx_mm_work_t work;uint32_t post[8];} scratch;
    memset(&scratch,0x57,sizeof(scratch));
    fzx_machine_menu_snapshot_t f={0};f.screen=(uint8_t)screen;f.character=(uint8_t)character;f.skin=(uint8_t)skin;f.unlocked=6;
    for(unsigned i=0;i<30;++i)if(slots[i]==character)f.slot=(uint8_t)i;
    unsigned total=0,maximum=0;
    for(unsigned frame=0;frame<(screen?419u:209u);++frame) {
        f.model_ticks=frame;unsigned count=0;
        if(!fzx_mm_render(&pack,&charts,(const int32_t (*)[18])cameras,slots,reflection,&f,&scratch.work,emit,NULL,&count))return 4;
        total+=count;if(count>maximum)maximum=count;
        for(unsigned i=0;i<8;++i)if(scratch.pre[i]!=0x57575757u||scratch.post[i]!=0x57575757u)return 5;
    }
    if(!total||!kind_count[FZX_MM_BASE]||(screen&&(!kind_count[FZX_MM_SHADOW]||!kind_count[FZX_MM_OUTLINE]||!kind_count[FZX_MM_REFLECTION])))return 6;
    unsigned count;
    if(fzx_mm_render(&pack,&charts,(const int32_t (*)[18])cameras,slots,reflection,&f,&scratch.work,reject,NULL,&count)||count)return 7;
    const uint8_t saved=slots[0];slots[0]=slots[1];
    if(fzx_mm_render(&pack,&charts,(const int32_t (*)[18])cameras,slots,reflection,&f,&scratch.work,emit,NULL,&count))return 7;
    slots[0]=saved;
    sh2z_view_t view;sh2z_mat34_t model;
    if(fzx_mm_view(&pack,cameras[0],629,65536,160,120,&view,&model)||
       fzx_mm_view(&pack,cameras[0],0,0,160,120,&view,&model))return 7;
    printf("%u %u %u %u %u %u %u\n",total,maximum,digest,kind_count[0],kind_count[1],kind_count[2],kind_count[3]);
    free(b);free(c);free(v);free(slots);free(reflection);return 0;
}
