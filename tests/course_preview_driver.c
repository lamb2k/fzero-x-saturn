#include "fzx_course_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned emitted;
static uint32_t digest=2166136261u;
static bool reject(void *context,const fzx_course_preview_triangle_t *t){(void)context;(void)t;return false;}
static bool emit(void *context,const fzx_course_preview_triangle_t *t) {
    (void)context;
    for(unsigned i=0;i<3;++i) {
        if(t->x[i]<-1024 || t->x[i]>1023 || t->y[i]<-1024 || t->y[i]>1023)return false;
        digest=(digest^(uint16_t)t->x[i])*16777619u;
        digest=(digest^(uint16_t)t->y[i])*16777619u;
    }
    ++emitted;return true;
}
int main(int argc,char **argv) {
    if(argc<2 || argc>3)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    if(fseek(f,0,SEEK_END))return 2;
    const long size=ftell(f);if(size<0 || size>512000)return 2;
    rewind(f);uint8_t *data=malloc((size_t)size);if(!data)return 2;
    if(fread(data,1,(size_t)size,f)!=(size_t)size)return 2;
    fclose(f);uint32_t hash=2166136261u;
    for(long i=0;i<size;++i)hash=(hash^data[i])*16777619u;
    fzx_course_preview_pack_t p={0};
    if(!fzx_course_preview_open(&p,data,(uint32_t)size,hash)){free(data);return 1;}
    if(fzx_course_preview_open(&p,data,(uint32_t)size,hash^1u))return 3;
    struct { uint32_t guard0[8];sh2z_pos16_t pos[FZX_COURSE_PREVIEW_VERTEX_LIMIT];
        uint32_t guard1[8];sh2z_view_vertex_t v[FZX_COURSE_PREVIEW_VERTEX_LIMIT];uint32_t guard2[8]; } scratch;
    memset(&scratch,0x5a,sizeof(scratch));
    FILE *dump=argc==3?fopen(argv[2],"wb"):NULL;if(argc==3 && !dump)return 2;
    unsigned total=0,maximum=0;
    for(unsigned yaw=0;yaw<360;++yaw)for(unsigned c=0;c<24;++c) {
        unsigned count,n;sh2z_view_t view;
        if(!fzx_course_preview_gather(&p,c,scratch.pos,FZX_COURSE_PREVIEW_VERTEX_LIMIT,&n) ||
           !fzx_course_preview_view(&p,yaw,0,&view) ||
           !sh2z_view_batch_divu(scratch.v,&view,scratch.pos,sizeof(*scratch.pos),n))return 3;
        if(dump && fwrite(scratch.v,sizeof(*scratch.v),n,dump)!=n)return 2;
        if(!fzx_course_preview_render(&p,c,yaw,0,scratch.pos,scratch.v,FZX_COURSE_PREVIEW_VERTEX_LIMIT,emit,NULL,&count))return 3;
        total+=count;if(count>maximum)maximum=count;
    }
    /* Exercise carousel clipping and callback capacity failures. */
    for(int shift=-6400;shift<=6800;shift+=80) {
        unsigned count;
        if(!fzx_course_preview_render(&p,0,45,shift,scratch.pos,scratch.v,FZX_COURSE_PREVIEW_VERTEX_LIMIT,emit,NULL,&count))return 3;
    }
    for(unsigned i=0;i<8;++i)if(scratch.guard0[i]!=0x5a5a5a5au ||
        scratch.guard1[i]!=0x5a5a5a5au || scratch.guard2[i]!=0x5a5a5a5au)return 4;
    unsigned n;sh2z_view_t view;
    if(fzx_course_preview_render(&p,0,0,0,scratch.pos,scratch.v,FZX_COURSE_PREVIEW_VERTEX_LIMIT,reject,NULL,&n) || n)return 4;
    if(fzx_course_preview_gather(&p,24,scratch.pos,FZX_COURSE_PREVIEW_VERTEX_LIMIT,&n) ||
       fzx_course_preview_gather(&p,0,scratch.pos,0,&n) || fzx_course_preview_view(&p,360,0,&view))return 4;
    if(dump)fclose(dump);
    printf("%u %u %u %u %u\n",total,maximum,emitted,digest,p.vertices);
    free(data);return 0;
}
