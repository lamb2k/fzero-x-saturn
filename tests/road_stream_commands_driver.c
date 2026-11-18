#define _GNU_SOURCE
#include "road_stream_commands.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);return 1;}}while(0)
static fzx_road_stream_command_t packets[3];
static vdp1_cmdt_t commands[4];
static void seed(void){
    memset(packets,0,sizeof(packets));memset(commands,0x5a,sizeof(commands));
    for(unsigned i=0;i<3;++i){
        packets[i].material=(fzx_road_stream_material_t){64,256,16,8,(uint16_t)(i==1),0};
        packets[i].solid=(uint16_t)(i==2);
        for(unsigned j=0;j<4;++j){packets[i].x[j]=(int16_t)(i*100+j*21-230);
            packets[i].y[j]=(int16_t)(i*70+j*14-110);packets[i].shade[j]=(uint16_t)(0x8000+i*100+j*27);}
    }
}
int main(void){
    CHECK(mmap((void *)0x25c00000u,0x80000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    void *pixels=(void *)0x25c01000u,*shades=(void *)0x25c40000u;
    uint32_t count=99;seed();fzx_road_stream_command_t original[3];memcpy(original,packets,sizeof(original));
    CHECK(fzx_road_stream_commands(commands,3,packets,3,pixels,1024,shades,24,&count)&&count==3);
    CHECK(!memcmp(original,packets,sizeof(original)));
    for(unsigned i=0;i<3;++i){
        const vdp1_cmdt_t *c=commands+i;const fzx_road_stream_command_t *p=packets+i;
        CHECK(c->kind==(i==2?4u:2u));
        CHECK(c->mode.end_code_disable==1&&c->mode.trans_pixel_disable==1);
        CHECK(c->mode.user_clipping_mode==2&&c->mode.cc_mode==VDP1_CMDT_CC_GOURAUD);
        CHECK(c->mode.color_mode==(i==1?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768));
        CHECK(c->cmd_colr==(i==2?0xc210:i==1?0x220:0));
        CHECK(c->cmd_xa==p->x[0]&&c->cmd_xb==p->x[1]&&c->cmd_xc==p->x[2]&&c->cmd_xd==p->x[3]);
        CHECK(c->cmd_ya==p->y[0]&&c->cmd_yb==p->y[1]&&c->cmd_yc==p->y[2]&&c->cmd_yd==p->y[3]);
        CHECK(c->cmd_grda==0x8000+i&&!memcmp((uint16_t *)shades+i*4,p->shade,8));
        if(i<2)CHECK(c->texture==0x25c01040u&&c->width==16&&c->height==8);
        else CHECK(!c->texture&&!c->width&&!c->height);
    }
    unsigned char untouched[sizeof(vdp1_cmdt_t)];memset(untouched,0x5a,sizeof(untouched));
    CHECK(!memcmp(commands+3,untouched,sizeof(untouched)));
    for(unsigned bad=0;bad<25;++bad){
        seed();count=99;uint32_t capacity=3,n=3,bytes=1024,sbytes=24;
        void *p=pixels,*s=shades;vdp1_cmdt_t *c=commands;const fzx_road_stream_command_t *q=packets;
        switch(bad){
        case 0:capacity=2;break;case 1:sbytes=23;break;case 2:p=(void *)0x25c01001u;break;
        case 3:s=(void *)0x25c40001u;break;case 4:p=(void *)0x25bffff8u;break;
        case 5:p=(void *)0x25c7fff8u;break;case 6:s=(void *)0x25c7fff8u;break;
        case 7:s=p;break;case 8:packets[1].material.pixels=65;break;
        case 9:packets[1].material.palette=257;break;
        case 10:packets[1].material.palette=1016;break;
        case 11:packets[0].material.pixels=1000;break;
        case 12:packets[1].material.mode=2;break;
        case 13:packets[0].material.width=7;break;
        case 14:packets[0].material.width=512;break;
        case 15:packets[0].material.height=0;break;
        case 16:packets[0].material.height=256;break;
        case 17:packets[0].material.reserved=1;break;
        case 18:packets[0].solid=2;break;
        case 19:c=NULL;break;case 20:q=NULL;break;case 21:bytes=0;break;
        case 22:c=(vdp1_cmdt_t *)((char *)commands+1);break;
        case 23:q=(const fzx_road_stream_command_t *)((const char *)packets+1);break;
        case 24:c=(vdp1_cmdt_t *)shades;break;
        }
        CHECK(!fzx_road_stream_commands(c,capacity,q,n,p,bytes,s,sbytes,&count)&&count==0);
    }
    seed();count=99;
    CHECK(fzx_road_stream_commands(commands,0,packets,0,pixels,1024,shades,0,&count)&&!count);
    CHECK(!fzx_road_stream_commands(commands,3,packets,3,pixels,1024,shades,24,NULL));
    CHECK(!fzx_road_stream_commands(commands,3,packets,3,pixels,1024,shades,24,(uint32_t *)((char *)&count+1)));
    /* Exact last-byte boundaries: RGB16=256 bytes, CLUT16=64 bytes plus32 palette. */
    packets[0].material.pixels=768;packets[1].material.pixels=960;packets[1].material.palette=992;
    CHECK(fzx_road_stream_commands(commands,3,packets,3,pixels,1024,(void *)0x25c7ffe8u,24,&count)&&count==3);
    puts("ROAD_STREAM_COMMANDS_HOST_PASS RGB CLUT16 opaque solid coordinates Gouraud 25 invalid boundaries");return 0;
}
