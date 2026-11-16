/* Offline diagnostic charts. Every source triangle remains a native command.
 * Baked view-dependent lighting is not a dynamic race renderer. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "fzx_model.h"
#include "model_probe.h"

static unsigned dimension(double length,unsigned multiple)
{
    unsigned n=(unsigned)ceil(length)+1;
    if(n<2)n=2;
    n=(n+multiple-1)/multiple*multiple;
    return n>128?128:n;
}
static double distance(const fzx_model_vertex_t *a,const fzx_model_vertex_t *b)
{return hypot((b->x_q4-a->x_q4)/16.0,(b->y_q4-a->y_q4)/16.0);}
static bool valid(const fzx_model_view_t *v)
{
    const unsigned textures=sizeof(fzx_model_textures)/sizeof(fzx_model_textures[0]);
    if(!v->vertex_count||!v->triangle_count||v->triangle_count>509||!v->material_count)return false;
    for(unsigned i=0;i<textures;++i)if(!fzx_material_texture_valid(&fzx_model_textures[i]))return false;
    for(unsigned i=0;i<v->vertex_count;++i){
        const fzx_model_vertex_t *p=&v->vertices[i];
        if(!p->inverse_z||p->inverse_z>32767||p->x_q4 < -4096||p->x_q4>8192||
           p->y_q4 < -4096||p->y_q4>8192)return false;
    }
    for(unsigned i=0;i<v->material_count;++i)if(v->materials[i].texture>=textures)return false;
    for(unsigned i=0;i<v->triangle_count;++i){
        if(v->triangles[i].material>=v->material_count)return false;
        for(unsigned j=0;j<3;++j)if(v->triangles[i].vertex[j]>=v->vertex_count)return false;
    }
    return true;
}
static uint16_t pixel(const fzx_model_vertex_t *a,const fzx_model_vertex_t *b,
                      const fzx_model_vertex_t *c,double s,double t,
                      const fzx_model_material_t *m)
{
    const fzx_model_vertex_t *v[3]={a,b,c};
    const double weight[3]={(1-t)*(1-s),(1-t)*s,t};
    double denominator=0,u=0,w=0,channels[4]={0};
    for(unsigned i=0;i<3;++i){
        const double q=weight[i]*v[i]->inverse_z;
        denominator+=q;
        u+=q*fzx_material_vertex_uv(v[i]->u_q5,65535);
        w+=q*fzx_material_vertex_uv(v[i]->v_q5,65535);
        channels[0]+=q*v[i]->shade.r;channels[1]+=q*v[i]->shade.g;
        channels[2]+=q*v[i]->shade.b;channels[3]+=q*v[i]->shade.a;
    }
    fzx_material_color_t shade={(uint16_t)(channels[0]/denominator),
        (uint16_t)(channels[1]/denominator),(uint16_t)(channels[2]/denominator),
        (uint16_t)(channels[3]/denominator)};
    fzx_material_combine_t combine=m->combine;
    if(m->body_environment)memcpy(combine.environment,fzx_model_body,3);
    return fzx_material_pixel(&combine,fzx_material_sample(&fzx_model_textures[m->texture],
        (int32_t)(u/denominator),(int32_t)(w/denominator)),shade,fzx_model_fog);
}
int main(void)
{
    printf("/* Generated diagnostic VDP1 charts; keep retail output private. */\n");
    printf("#define FZX_CHART_FIXTURE %u\n#define FZX_CHART_VIEWS %u\n",FZX_MODEL_FIXTURE,FZX_MODEL_VIEW_COUNT);
    printf("typedef struct { const uint16_t *pixels; uint16_t width,height,source_triangle,material; int16_t xy[6]; uint32_t depth; } chart_t;\n");
    printf("typedef struct { const chart_t *charts; uint16_t count; uint32_t bytes,hash; } chart_view_t;\n");
    uint32_t totals[FZX_MODEL_VIEW_COUNT],hashes[FZX_MODEL_VIEW_COUNT];
    unsigned widths[512],heights[512];
    for(unsigned k=0;k<FZX_MODEL_VIEW_COUNT;++k){
        const fzx_model_view_t *view=&fzx_model_views[k];
        if(!valid(view))return 2;
        totals[k]=0;hashes[k]=2166136261u;
        for(unsigned j=0;j<view->triangle_count;++j){
            const fzx_model_triangle_t *tri=&view->triangles[j];
            const fzx_model_vertex_t *a=&view->vertices[tri->vertex[0]],
                *b=&view->vertices[tri->vertex[1]],*c=&view->vertices[tri->vertex[2]];
            widths[j]=dimension(distance(a,b),8);
            heights[j]=dimension(fmax(distance(a,c),distance(b,c)),1);
            totals[k]+=widths[j]*heights[j]*2;
            printf("static const uint16_t chart_%u_%u[]={\n",k,j);
            for(unsigned y=0;y<heights[j];++y){
                for(unsigned x=0;x<widths[j];++x){
                    uint16_t p=pixel(a,b,c,(double)x/(widths[j]-1),(double)y/(heights[j]-1),&view->materials[tri->material]);
                    hashes[k]=(hashes[k]^(p>>8))*16777619u;
                    hashes[k]=(hashes[k]^(p&255))*16777619u;
                    printf("%u,",p);
                }
                puts("");
            }
            puts("};");
        }
        printf("static const chart_t charts_%u[]={\n",k);
        for(unsigned j=0;j<view->triangle_count;++j){
            const fzx_model_triangle_t *tri=&view->triangles[j];
            printf("{chart_%u_%u,%u,%u,%u,%u,{",k,j,widths[j],heights[j],j,tri->material);
            uint32_t depth=0;
            for(unsigned i=0;i<3;++i){
                const fzx_model_vertex_t *v=&view->vertices[tri->vertex[i]];
                printf("%ld,%ld%s",lround(v->x_q4/16.0),lround(v->y_q4/16.0),i==2?"":",");
                depth+=v->inverse_z;
            }
            printf("},%u},\n",depth);
        }
        puts("};");
        if(totals[k]>400*1024)return 3;
        fprintf(stderr,"view=%u triangles=%u chart_bytes=%u hash=%08x\n",k,view->triangle_count,totals[k],hashes[k]);
    }
    puts("static const chart_view_t chart_views[]={");
    for(unsigned k=0;k<FZX_MODEL_VIEW_COUNT;++k)
        printf("{charts_%u,%u,%u,%uu},\n",k,fzx_model_views[k].triangle_count,totals[k],hashes[k]);
    puts("};");
    return ferror(stdout)?4:0;
}
