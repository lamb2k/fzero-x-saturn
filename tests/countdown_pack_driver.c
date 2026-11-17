#include "fzx_countdown_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"countdown pack check %d\n",__LINE__);return 2;}}while(0)
static unsigned be16(const uint8_t *p){return (unsigned)p[0]*256u+p[1];}
static uint32_t be32(const uint8_t *p){return (uint32_t)be16(p)*65536u+be16(p+2);}
int main(int argc,char **argv){
    if(argc!=5)return 3;
    FILE *file=fopen(argv[1],"rb");if(!file)return 3;
    CHECK(!fseek(file,0,SEEK_END));const long size=ftell(file);CHECK(size>=0&&size<=4096);
    rewind(file);uint8_t data[4096];CHECK(fread(data,1,(size_t)size,file)==(size_t)size);fclose(file);
    fzx_countdown_pack_t pack={0},before=pack;
    const bool admitted=fzx_countdown_pack_open(&pack,data,(uint32_t)size,(uint32_t)strtoul(argv[4],NULL,0),
        (uint32_t)strtoul(argv[2],NULL,0),(uint32_t)strtoul(argv[3],NULL,0));
    if(!admitted){CHECK(!memcmp(&pack,&before,sizeof(pack)));return 1;}
    sh2z_pos16_t positions[92],saved[92];memset(positions,0x35,sizeof(positions));memcpy(saved,positions,sizeof(saved));
    CHECK(!fzx_countdown_positions(&pack,positions,91));CHECK(!memcmp(positions,saved,sizeof(saved)));
    CHECK(fzx_countdown_positions(&pack,positions,92));
    for(unsigned i=0;i<92;++i){const uint8_t *p=data+64+i*16;
        CHECK(positions[i].x==(int16_t)be16(p)&&positions[i].y==(int16_t)be16(p+2)&&positions[i].z==(int16_t)be16(p+4));}
    unsigned checks=0;
    for(unsigned panel=0;panel<5;++panel)for(unsigned mouth=0;mouth<2;++mouth){
        const fzx_countdown_phase_t phase={true,mouth!=0,(uint8_t)panel};
        for(unsigned q=0;q<23;++q){fzx_countdown_image_t image;
            CHECK(fzx_countdown_image(&pack,q,&phase,&image));
            const unsigned state=q<20?0:q<22?mouth:panel?panel-1:0;
            const uint8_t *source=data+1904+(be16(data+1536+q*16+8)+state)*16;
            CHECK(image.texture==be32(source+4)&&image.width==be16(source)&&image.height==be16(source+2));
            CHECK(image.solid==(q==22&&panel==0));++checks;
        }
    }
    fzx_countdown_image_t image={0},old=image;fzx_countdown_phase_t phase={false,false,0};
    CHECK(!fzx_countdown_image(&pack,0,&phase,&image)&&!memcmp(&old,&image,sizeof(image)));
    phase.visible=true;phase.panel=5;CHECK(!fzx_countdown_image(&pack,22,&phase,&image));
    phase.panel=0;CHECK(!fzx_countdown_image(&pack,23,&phase,&image));
    printf("countdown pack positions=92 selections=%u\n",checks);return 0;
}
