/* Emit eight private, big-endian RGB1555 frames on stdout for local checking. */
#include <stdio.h>
#include "fzx_model.h"
#include "model_probe.h"
static uint16_t pixels[128*128];
static uint32_t depth[128*128];
int main(void)
{
    fzx_model_image_t image={pixels,depth,128,128,0,0,0};
    for(unsigned k=0;k<FZX_MODEL_VIEW_COUNT;++k){
        if(!fzx_model_render(&fzx_model_views[k],fzx_model_textures,8,fzx_model_body,fzx_model_fog,&image))return 1;
        uint32_t hash=2166136261u;
        for(unsigned i=0;i<128u*128u;++i){
            const uint8_t p[2]={(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};
            hash=(hash^p[0])*16777619u;hash=(hash^p[1])*16777619u;
            if(fwrite(p,1,2,stdout)!=2)return 2;
        }
        fprintf(stderr,"view=%u hash=%08x pixels=%u tested=%u triangles=%u\n",k,hash,image.drawn_pixels,image.tested_pixels,image.drawn_triangles);
    }
    return 0;
}
