/* Point-sampled source material charts for the dynamic CLUT experiment. */
#define main unused_rgb_chart_main
#include "../vdp1_probe/bake.c"
#undef main
static int32_t nearest(int32_t value)
{
    value+=128;
    return (value/256-(value%256<0))*256;
}
static bool same(fzx_material_color_t a,fzx_material_color_t b)
{return a.r==b.r&&a.g==b.g&&a.b==b.b;}
int main(void)
{
    printf("/* Generated native point-sampled charts; retail output is private. */\n");
    printf("#define FZX_PALETTE_FIXTURE %u\n",FZX_MODEL_FIXTURE);
    printf("typedef struct { const uint8_t *indices; const fzx_material_color_t *base; uint16_t width,height,colors,source_triangle,material; } indexed_chart_t;\n");
    const unsigned views[5]={0,4,5,6,7};
    uint16_t widths[512],heights[512],counts[512];
    for(unsigned lod=0;lod<5;++lod){
        const fzx_model_view_t *view=&fzx_model_views[views[lod]];
        if(!valid(view))return 2;
        unsigned total=0;
        for(unsigned k=0;k<view->triangle_count;++k){
            const fzx_model_triangle_t *tri=&view->triangles[k];
            const fzx_model_material_t *m=&view->materials[tri->material];
            const fzx_material_texture_t *texture=&fzx_model_textures[m->texture];
            if(texture->s_shift||texture->t_shift)return 3;
            double u[3],v[3];
            for(unsigned j=0;j<3;++j){const fzx_model_vertex_t *p=&view->vertices[tri->vertex[j]];
                u[j]=fzx_material_vertex_uv(p->u_q5,65535);v[j]=fzx_material_vertex_uv(p->v_q5,65535);}
            widths[k]=(uint16_t)dimension(hypot(u[1]-u[0],v[1]-v[0])/256,8);
            heights[k]=(uint16_t)dimension(fmax(hypot(u[2]-u[0],v[2]-v[0]),hypot(u[2]-u[1],v[2]-v[1]))/256,2);
            uint8_t indices[128*128];fzx_material_color_t base[16];unsigned count=0;
            fzx_material_combine_t combine=m->combine;
            if(m->body_environment)memcpy(combine.environment,fzx_model_body,3);
            for(unsigned y=0;y<heights[k];++y)for(unsigned x=0;x<widths[k];++x){
                const double s=(double)x/(widths[k]-1),t=(double)y/(heights[k]-1),a=(1-t)*(1-s),b=(1-t)*s;
                fzx_material_color_t texel=fzx_material_sample(texture,nearest((int32_t)(u[0]*a+u[1]*b+u[2]*t)),nearest((int32_t)(v[0]*a+v[1]*b+v[2]*t)));
                const fzx_material_color_t white={65535,65535,65535,0};
                fzx_material_color_t color=fzx_material_shade(&combine,texel,white,fzx_model_fog);
                unsigned index=0;while(index<count&&!same(base[index],color))++index;
                if(index==count){if(count==16)return 4;base[count++]=color;}
                indices[y*widths[k]+x]=(uint8_t)index;
            }
            counts[k]=(uint16_t)count;total+=widths[k]*heights[k]/2;
            printf("static const uint8_t indices_%u_%u[]={\n",lod,k);
            for(unsigned i=0;i<(unsigned)widths[k]*heights[k];i+=2){printf("%u,",(unsigned)(indices[i]<<4)|indices[i+1]);if(!(i%64))puts("");}puts("};");
            printf("static const fzx_material_color_t base_%u_%u[]={",lod,k);
            for(unsigned i=0;i<count;++i)printf("{%u,%u,%u,65535},",base[i].r,base[i].g,base[i].b);
            puts("};");
        }
        if(total>400*1024)return 5;
        printf("static const indexed_chart_t indexed_charts_%u[]={\n",lod);
        for(unsigned k=0;k<view->triangle_count;++k)printf("{indices_%u_%u,base_%u_%u,%u,%u,%u,%u,%u},\n",lod,k,lod,k,widths[k],heights[k],counts[k],k,view->triangles[k].material);
        puts("};");fprintf(stderr,"lod=%u triangles=%u bytes=%u\n",lod,view->triangle_count,total);
    }
    return ferror(stdout)?6:0;
}
