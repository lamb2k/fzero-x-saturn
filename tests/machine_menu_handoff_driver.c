/* HOST-only driver; fixtures and traces stay outside the source tree. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ref.h"
extern int machine_handoff_probe(unsigned int,float,int,unsigned int[27]);
extern void machine_handoff_reference(void);

#if FZX_SOURCE_MACHINE_MENU
static unsigned char *gfx_allocation,*gfx_memory;
static unsigned int gfx_bytes;
static int gfx_guards(void) {
    if(!gfx_memory)return 1;
    for(unsigned int i=0;i<32;i++)
        if(gfx_memory[(int)i-32]!=0xa5 || gfx_memory[gfx_bytes+i]!=0xa5)return 0;
    return 1;
}
static int gfx_bind(void) {
    gfx_bytes=ref_race_gfx_bytes();
    if(!gfx_bytes || gfx_bytes>1024u*1024u)return 1;
    gfx_allocation=calloc(1,gfx_bytes+127u);if(!gfx_allocation)return 2;
    gfx_memory=(unsigned char *)(((uintptr_t)gfx_allocation+63u)&~(uintptr_t)31u);
    memset(gfx_memory-32,0xa5,32);memset(gfx_memory+gfx_bytes,0xa5,32);
    if(!ref_race_gfx_bind(NULL,gfx_bytes) || !ref_race_gfx_bind(gfx_memory+1,gfx_bytes) ||
       !ref_race_gfx_bind(gfx_memory,gfx_bytes-1u) || !ref_race_gfx_bind(gfx_memory,0))return 3;
    if(ref_race_gfx_bind(gfx_memory,gfx_bytes))return 4;
    /* Invalid calls after admission must not discard the admitted allocation. */
    if(!ref_race_gfx_bind(NULL,gfx_bytes) || !ref_race_gfx_bind(gfx_memory+1,gfx_bytes) ||
       !ref_race_gfx_bind(gfx_memory,gfx_bytes-1u))return 5;
    return gfx_guards()?0:6;
}
#endif

static uint64_t timeline(void) {
    uint64_t hash=UINT64_C(14695981039346656037);
    unsigned int i,j;
    RefState state;
    for(i=0;i<600;i++) {
        ref_tick(0x8000,(signed char)(i%120<60?8:-8),0);
        ref_state(&state);
        for(j=0;j<sizeof(state);j++)hash=(hash^((unsigned char *)&state)[j])*UINT64_C(1099511628211);
    }
    return hash;
}

int main(int argc,char **argv) {
    unsigned char course[REF_COURSE_RECORD_SIZE],boot[REF_ROM_BOOT_SIZE];
    unsigned int before[27],after[27],i,skin;
    float engine;
    uint64_t first,second;
    FILE *fp;
    int mode,probe,source_expected=0,storage;
    if(argc!=5 && argc!=6)return 2;
    storage=argc==6?atoi(argv[5]):0;
    if(storage<0 || storage>2)return 2;
    mode=atoi(argv[2]);skin=(unsigned int)strtoul(argv[3],NULL,10);engine=strtof(argv[4],NULL);
    fp=fopen(argv[1],"rb");if(!fp)return 3;
    if(fread(course,1,sizeof(course),fp)!=sizeof(course))return 4;
    if(fclose(fp))return 5;
    for(i=0;i<sizeof(boot);i++)boot[i]=(unsigned char)(i*151+77);
    ref_set_rom_boot(boot);
#if FZX_SOURCE_MACHINE_MENU
    source_expected=1;
    if(storage==1 && gfx_bind())return 14;
    if(mode && ref_race_set_selection(0,skin,engine))return 6;
    if(mode==2) {
        static const float invalid[]={-0.001f,1.001f,INFINITY,-INFINITY,NAN};
        if(!ref_race_set_selection(1,0,0.5f) || !ref_race_set_selection(29,0,0.5f) ||
           !ref_race_set_selection(~0u,0,0.5f) || !ref_race_set_selection(0,4,0.5f) ||
           !ref_race_set_selection(0,~0u,0.5f))return 7;
        for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
            if(!ref_race_set_selection(0,0,invalid[i]))return 8;
    }
#else
    if(storage)return 15;
    if((mode!=0 && mode!=3) || skin || engine!=0.5f)return 9;
    if(mode==3){machine_handoff_reference();source_expected=1;}
#endif
    if(ref_load_course(course,1))return 10;
    /* The external-only closure must reject an absent bind before touching it. */
    if(storage==2)return ref_race_init();
    if(ref_race_init())return 10;
#if FZX_SOURCE_MACHINE_MENU
    if(!gfx_guards())return 16;
#endif
    probe=machine_handoff_probe(skin,engine,source_expected,before);if(probe)return 20+probe;
    first=timeline();
#if FZX_SOURCE_MACHINE_MENU
    if(!gfx_guards())return 16;
#endif
    ref_race_restart();
#if FZX_SOURCE_MACHINE_MENU
    if(!gfx_guards())return 16;
#endif
    if(machine_handoff_probe(skin,engine,source_expected,after) || memcmp(before,after,sizeof(before)))return 12;
    second=timeline();if(first!=second)return 13;
#if FZX_SOURCE_MACHINE_MENU
    if(!gfx_guards())return 16;
    free(gfx_allocation);
#endif
    for(i=0;i<27;i++)printf("%08x%c",before[i],i==26?'\n':' ');
    printf("%016llx\n",(unsigned long long)first);
    return 0;
}
