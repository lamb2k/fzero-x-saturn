/* Moving native source geometry and per-triangle CLUT/Gouraud experiment. */
#include <yaul.h>
#include <string.h>
#include "machine.h"
#include "metrics.h"
_Static_assert(FZX_PALETTE_FIXTURE==FZX_EXPECT_PALETTE_FIXTURE,"Wrong palette provenance");
static vdp1_cmdt_t commands[512];
static uint16_t order[509],addresses[509];
static uint32_t depths[509];
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
static vdp1_cmdt_list_t list={.cmdts=commands,.count=3};
static vdp1_vram_partitions_t partitions;
volatile uint32_t video_fields;
volatile uint32_t fzx_palette_status,fzx_palette_case,fzx_palette_hold,fzx_palette_completed_case;
volatile uint32_t fzx_palette_sequence,fzx_palette_upload_ticks,fzx_palette_build_ticks,fzx_palette_project_ticks;
volatile uint32_t fzx_palette_wait_fields,fzx_palette_vertex_hash,fzx_palette_light_hash;
volatile uint32_t fzx_palette_bytes,fzx_palette_count,fzx_palette_saturated;
volatile uint32_t fzx_palette_texture_hash,fzx_palette_command_hash,fzx_palette_vram_hash;
static volatile uint32_t timer_high;
static void overflow(void){timer_high+=65536u;}
static uint32_t now(void){uint32_t a,b;uint16_t low;do{a=timer_high;low=cpu_frt_count_get();b=timer_high;}while(a!=b);return a+low;}
static void vblank(void *p){(void)p;++video_fields;}
void user_init(void)
{
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,VDP2_TVMD_HORZ_NORMAL_A,VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x1fffe),RGB1555(1,2,2,5));vdp2_sprite_priority_set(0,6);
    vdp1_env_t env;vdp1_env_default_init(&env);env.erase_color=RGB1555(0,0,0,0);
    vdp1_env_set(&env);vdp1_sync_interval_set(-1);vdp_sync_vblank_out_set(vblank,NULL);vdp2_tvmd_display_set();
}
static bool upload(unsigned lod)
{
    const machine_model_t *m=&machine_models[lod];
    if(!fzx_machine_plan_valid(m->vertices,m->plan)||m->plan->vertex_count!=m->vertex_count||!m->triangle_count||m->triangle_count>509||m->vertex_count>1024)return false;
    uint32_t offset=0,hash=2166136261u;
    for(unsigned i=0;i<m->triangle_count;++i){
        const indexed_chart_t *c=&m->charts[i];
        if(c->source_triangle!=i||!c->width||(c->width%8)||c->width>128||c->height<2||c->height>128||
           !c->colors||c->colors>16)return false;
        uint32_t size=(uint32_t)c->width*c->height/2;
        if((size%8)||offset>partitions.texture_size||size>partitions.texture_size-offset)return false;
        addresses[i]=(uint16_t)((((uintptr_t)partitions.texture_base&0x7ffffu)+offset)>>3);
        volatile const uint8_t *dest=(volatile const uint8_t*)partitions.texture_base+offset;
        memcpy((uint8_t*)partitions.texture_base+offset,c->indices,size);
        for(unsigned j=0;j<size;++j){if(dest[j]!=c->indices[j])return false;hash=(hash^dest[j])*16777619u;}
        offset+=size;
    }
    fzx_palette_texture_hash=hash;fzx_palette_bytes=offset;fzx_palette_count=m->triangle_count;return true;
}
static bool build(unsigned lod)
{
    const machine_model_t *m=&machine_models[lod];
    uint32_t saturated=0,hash=2166136261u;
    for(unsigned i=0;i<m->triangle_count;++i){
        const fzx_model_triangle_t *tri=&m->triangles[i];const indexed_chart_t *chart=&m->charts[i];
        fzx_material_color_t shade[3];depths[i]=0;order[i]=i;
        for(unsigned j=0;j<3;++j){if(tri->vertex[j]>=m->vertex_count)return false;
            const fzx_model_vertex_t *v=&projected[tri->vertex[j]];shade[j]=v->shade;depths[i]+=v->inverse_z;}
        fzx_machine_palette_t lit;
        if(!LIGHT(chart->base,chart->colors,shade,machine_fog,&lit))return false;
        saturated+=lit.saturated_channels;
        memcpy(&partitions.clut_base[i],lit.palette,32);memcpy(&partitions.gouraud_base[i],lit.gouraud,8);
        for(unsigned j=0;j<16;++j)hash=hash_word(hash,lit.palette[j]);
        for(unsigned j=0;j<4;++j)hash=hash_word(hash,lit.gouraud[j]);
    }
    for(unsigned i=1;i<m->triangle_count;++i){unsigned j=i;uint16_t key=order[i];
        while(j&&depths[order[j-1]]>depths[key]){order[j]=order[j-1];--j;}order[j]=key;}
    const vdp1_cmdt_draw_mode_t mode={.color_mode=1,.cc_mode=4,.end_code_disable=true,.trans_pixel_disable=true};
    for(unsigned i=0;i<m->triangle_count;++i){
        unsigned index=order[i];const indexed_chart_t *c=&m->charts[index];
        const fzx_model_triangle_t *tri=&m->triangles[index];vdp1_cmdt_t *cmd=&commands[i+2];
        memset(cmd,0,sizeof(*cmd));vdp1_cmdt_distorted_sprite_set(cmd);vdp1_cmdt_draw_mode_set(cmd,mode);
        cmd->cmd_srca=addresses[index];cmd->cmd_size=(uint16_t)((c->width/8)<<8|c->height);
        cmd->cmd_colr=(uint16_t)(((uintptr_t)&partitions.clut_base[index]&0x7ffffu)>>3);
        cmd->cmd_grda=(uint16_t)(((uintptr_t)&partitions.gouraud_base[index]&0x7ffffu)>>3);
        const fzx_model_vertex_t *a=&projected[tri->vertex[0]],*b=&projected[tri->vertex[1]],*d=&projected[tri->vertex[2]];
        cmd->cmd_xa=(a->x_q4+8)/16+96;cmd->cmd_ya=(a->y_q4+8)/16+40;
        cmd->cmd_xb=(b->x_q4+8)/16+96;cmd->cmd_yb=(b->y_q4+8)/16+40;
        cmd->cmd_xc=cmd->cmd_xd=(d->x_q4+8)/16+96;cmd->cmd_yc=cmd->cmd_yd=(d->y_q4+8)/16+40;
    }
    memset(&commands[m->triangle_count+2],0,sizeof(commands[0]));vdp1_cmdt_end_set(&commands[m->triangle_count+2]);
    list.count=m->triangle_count+3;fzx_palette_light_hash=hash;fzx_palette_saturated=saturated;return true;
}
/* Read back submitted data outside the measured projection/build phases. */
static void submission_hashes(void)
{
    uint32_t hash=2166136261u;
    const volatile uint16_t *words=(const volatile uint16_t*)commands;
    for(unsigned i=0;i<list.count*sizeof(commands[0])/2;++i)hash=hash_word(hash,words[i]);
    fzx_palette_command_hash=hash;hash=2166136261u;
    for(unsigned i=0;i<fzx_palette_count;++i){
        const volatile uint16_t *clut=(const volatile uint16_t*)&partitions.clut_base[i];
        const volatile uint16_t *gouraud=(const volatile uint16_t*)&partitions.gouraud_base[i];
        for(unsigned j=0;j<16;++j)hash=hash_word(hash,clut[j]);
        for(unsigned j=0;j<4;++j)hash=hash_word(hash,gouraud[j]);
    }
    fzx_palette_vram_hash=hash;
    if(hash!=fzx_palette_light_hash){fzx_palette_status=3;for(;;){}}
}
int main(void)
{
    cpu_intc_mask_set(0);cpu_frt_init(CPU_FRT_CLOCK_DIV_32);cpu_frt_ovi_set(overflow);cpu_frt_interrupt_priority_set(12);
    dbgio_init();dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);dbgio_dev_font_load();
    vdp1_vram_partitions_set(512,400*1024,509,509);vdp1_vram_partitions_get(&partitions);
    const int16_vec2_t clip=INT16_VEC2_INITIALIZER(319,223),origin=INT16_VEC2_INITIALIZER(0,0);
    vdp1_cmdt_system_clip_coord_set(&commands[0]);vdp1_cmdt_vtx_system_clip_coord_set(&commands[0],clip);
    vdp1_cmdt_local_coord_set(&commands[1]);vdp1_cmdt_vtx_local_coord_set(&commands[1],origin);
    dbgio_printf("\x1b[H\x1b[2J\x1b[2;2H NATIVE MOVING MACHINE PROBE\n\x1b[4;2H %s\n\x1b[24;2H CLUT / GOURAUD / REVIEW PENDING",FZX_PALETTE_FIXTURE?"SYNTHETIC FIXTURE":"LOCAL SOURCE ART");
    dbgio_flush();vdp2_sync();unsigned loaded=5;
    for(;;){
        uint32_t field=video_fields;vdp1_sync_wait();vdp2_sync_wait();fzx_palette_wait_fields=video_fields-field;
        if(!fzx_palette_hold)fzx_palette_case=8+(video_fields/2)%64;
        unsigned selected=fzx_palette_case%MACHINE_POSES,lod=selected<4?0:selected<8?selected-3:0;
        if(loaded!=lod){uint32_t start=now();if(!upload(lod)){fzx_palette_status=2;for(;;){}}
            fzx_palette_upload_ticks=now()-start;loaded=lod;}
        const machine_model_t *m=&machine_models[lod];uint32_t start=now();
        if(!PROJECT(m,&machine_poses[selected])){fzx_palette_status=2;for(;;){}}
        fzx_palette_project_ticks=now()-start;fzx_palette_vertex_hash=hash_vertices(projected,m->vertex_count);
        start=now();if(!build(lod)){fzx_palette_status=2;for(;;){}}fzx_palette_build_ticks=now()-start;submission_hashes();
        vdp1_sync_cmdt_list_put(&list,0);vdp1_sync_render();vdp1_sync();vdp2_sync();
        fzx_palette_completed_case=selected;++fzx_palette_sequence;fzx_palette_status=1;
    }
}
