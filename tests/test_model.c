#include "fzx_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const uint8_t texels[2]={255,255};
    fzx_material_texture_t texture={0};
    /* Use the same validated I4 representation as the source converter. */
    texture.data=texels;
    texture.width=2;texture.height=2;
    texture.size=sizeof(texels);
    texture.format=FZX_TEX_I4;
    fzx_model_vertex_t vertices[3]={
        {0,0,256,0,0,{65535,65535,65535,65535}},
        {128,0,256,0,0,{65535,65535,65535,65535}},
        {0,128,256,0,0,{65535,65535,65535,65535}}};
    fzx_model_triangle_t triangle={{0,1,2},0};
    fzx_model_material_t material={0};
    memset(material.combine.primitive,255,3);
    vertices[0].shade.a=vertices[1].shade.a=vertices[2].shade.a=0;
    uint16_t pixels[64];uint32_t depth[64];
    fzx_model_image_t image={pixels,depth,8,8,0,0,0};
    fzx_model_view_t view={vertices,&triangle,&material,3,1,1};
    const uint8_t body[3]={255,255,255},fog[3]={0,0,0};
    assert(fzx_model_render(&view,&texture,1,body,fog,&image));
    assert(image.drawn_pixels==28);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
        assert(pixels[y*8+x]==(x+y<7?0xffff:0));
        assert(depth[y*8+x]==(x+y<7?65536u:0u));
    }
    uint16_t saved[64];memcpy(saved,pixels,sizeof(saved));
    triangle.vertex[2]=3;
    assert(!fzx_model_render(&view,&texture,1,body,fog,&image));
    assert(!memcmp(saved,pixels,sizeof(saved)));
    triangle.vertex[2]=2;
    /* Valid coordinate limits, area one: red-plane Q8 exceeds INT64_MAX.
     * The setup must reject this instead of overflowing under UBSan. */
    vertices[0].x_q4=-4096;vertices[0].y_q4=8192;
    vertices[1].x_q4=8192;vertices[1].y_q4=-4095;
    vertices[2].x_q4=8191;vertices[2].y_q4=-4094;
    for(unsigned i=0;i<3;++i){vertices[i].inverse_z=32767;vertices[i].shade.r=0;}
    vertices[1].shade.r=65535;
    assert(!fzx_model_render(&view,&texture,1,body,fog,&image));
    puts("MODEL coverage, depth, invalid indices and overflow rejection PASS");
    return 0;
}
