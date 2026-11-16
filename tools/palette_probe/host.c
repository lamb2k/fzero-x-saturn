/* Host projected-vertex/material oracle and private reference pixels. */
#include <stdio.h>
#include "machine.h"
#include "model_probe.h"
#include "metrics.h"
static fzx_model_vertex_t projected[1024];
#ifndef FZX_MACHINE_REUSE
#define FZX_MACHINE_REUSE 0
#endif
#if FZX_MACHINE_REUSE
static fzx_machine_position_t position_cache[1024];
static fzx_material_color_t normal_cache[1024];
#define LIGHT fzx_machine_light_reuse
#define PROJECT(M,P) fzx_machine_project_reuse((M)->vertices,(M)->plan,(P),machine_reflection_lut,position_cache,normal_cache,projected)
#else
#define LIGHT fzx_machine_light
#define PROJECT(M,P) fzx_machine_project((M)->vertices,(M)->vertex_count,(P),machine_reflection_lut,projected)
#endif
static uint16_t pixels[128*128];
static uint32_t depth[128*128];
int main(void)
{
    fzx_model_image_t image={pixels,depth,128,128,0,0,0};
    for(unsigned k=0;k<MACHINE_POSES;++k){
        unsigned lod=k<4?0:k<8?k-3:0;const machine_model_t *m=&machine_models[lod];
        if(!fzx_machine_plan_valid(m->vertices,m->plan)||m->plan->vertex_count!=m->vertex_count||!PROJECT(m,&machine_poses[k]))return 1;
        uint32_t hash=2166136261u,saturated=0,bytes=0;
        for(unsigned i=0;i<m->triangle_count;++i){
            const fzx_model_triangle_t *tri=&m->triangles[i];const indexed_chart_t *c=&m->charts[i];
            const fzx_material_color_t shade[3]={projected[tri->vertex[0]].shade,projected[tri->vertex[1]].shade,projected[tri->vertex[2]].shade};
            fzx_machine_palette_t lit;if(!LIGHT(c->base,c->colors,shade,machine_fog,&lit))return 2;
            saturated+=lit.saturated_channels;bytes+=(uint32_t)c->width*c->height/2;
            for(unsigned j=0;j<16;++j)hash=hash_word(hash,lit.palette[j]);
            for(unsigned j=0;j<4;++j)hash=hash_word(hash,lit.gouraud[j]);
        }
        fzx_model_view_t view=fzx_model_views[lod?lod+3:0];view.vertices=projected;
        if(!fzx_model_render(&view,fzx_model_textures,8,fzx_model_body,fzx_model_fog,&image))return 3;
        uint32_t image_hash=2166136261u;
        for(unsigned i=0;i<128*128;++i){image_hash=hash_word(image_hash,pixels[i]);const uint8_t bytes2[2]={(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};if(fwrite(bytes2,1,2,stdout)!=2)return 4;}
        fprintf(stderr,"case=%u vertices=%08x lights=%08x image=%08x saturated=%u triangles=%u bytes=%u\n",k,hash_vertices(projected,m->vertex_count),hash,image_hash,saturated,m->triangle_count,bytes);
    }
    return 0;
}
