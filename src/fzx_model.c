#include "fzx_model.h"
#include <limits.h>
#include <string.h>

static int32_t edge(const fzx_model_vertex_t *a,const fzx_model_vertex_t *b,int32_t x,int32_t y)
{
    return ((int32_t)b->x_q4-a->x_q4)*(y-a->y_q4)-((int32_t)b->y_q4-a->y_q4)*(x-a->x_q4);
}

static bool valid(const fzx_model_view_t *v,const fzx_material_texture_t *t,unsigned count,
                  const fzx_model_image_t *image)
{
    if(!v||!t||!count||!v->vertices||!v->triangles||!v->materials||
       !v->vertex_count||!v->triangle_count||!v->material_count||
       !image||!image->pixels||!image->depth||!image->width||!image->height||
       image->width>256||image->height>256) return false;
    for(unsigned i=0;i<count;++i)if(!fzx_material_texture_valid(&t[i]))return false;
    for(unsigned i=0;i<v->material_count;++i)if(v->materials[i].texture>=count)return false;
    for(unsigned i=0;i<v->vertex_count;++i){
        const fzx_model_vertex_t *p=&v->vertices[i];
        if(!p->inverse_z||p->inverse_z>32767||p->x_q4 < -4096||p->x_q4>8192||
           p->y_q4 < -4096||p->y_q4>8192)return false;
        const int64_t u=(int64_t)fzx_material_vertex_uv(p->u_q5,65535)*p->inverse_z;
        const int64_t w=(int64_t)fzx_material_vertex_uv(p->v_q5,65535)*p->inverse_z;
        if(u<INT32_MIN||u>INT32_MAX||w<INT32_MIN||w>INT32_MAX)return false;
    }
    for(unsigned i=0;i<v->triangle_count;++i){
        const fzx_model_triangle_t *p=&v->triangles[i];
        if(p->material>=v->material_count)return false;
        for(unsigned j=0;j<3;++j)if(p->vertex[j]>=v->vertex_count)return false;
    }
    return true;
}

static int32_t divide(int64_t numerator,int32_t denominator)
{
#if defined(FZX_MODEL_DIVU) && FZX_MODEL_DIVU && defined(__sh__)
    volatile int32_t *const dvsr=(volatile int32_t*)0xFFFFFF00u;
    volatile uint32_t *const high=(volatile uint32_t*)0xFFFFFF10u;
    volatile uint32_t *const low=(volatile uint32_t*)0xFFFFFF14u;
    *dvsr=denominator;*high=(uint32_t)((uint64_t)numerator>>32);
    *low=(uint32_t)numerator;return (int32_t)*low;
#else
    return (int32_t)(numerator/denominator);
#endif
}

static void attributes(const fzx_model_vertex_t *v,int32_t out[7])
{
    const int32_t z=v->inverse_z;
    out[0]=z;
    out[1]=fzx_material_vertex_uv(v->u_q5,65535)*z;
    out[2]=fzx_material_vertex_uv(v->v_q5,65535)*z;
    out[3]=(int32_t)v->shade.r*z;out[4]=(int32_t)v->shade.g*z;
    out[5]=(int32_t)v->shade.b*z;out[6]=(int32_t)v->shade.a*z;
}

static int minimum(int a,int b,int c){int r=a<b?a:b;return r<c?r:c;}
static int maximum(int a,int b,int c){int r=a>b?a:b;return r>c?r:c;}
static bool plane_q8(int64_t numerator,int32_t area,int64_t *result)
{
    /* Do not overflow when a large, mostly off-screen triangle carries UVs
     * near their signed limit. The remainder has the numerator's sign. */
    const int64_t whole=numerator/area;
    if(whole>(INT64_MAX-255)/256||whole<(INT64_MIN+255)/256)return false;
    *result=whole*256+((numerator%area)*256)/area;
    return true;
}
static bool plane_span_valid(int64_t base,int64_t dx,int64_t dy,unsigned width,unsigned height)
{
    const uint64_t a=(uint64_t)(base<0?-base:base),x=(uint64_t)(dx<0?-dx:dx),y=(uint64_t)(dy<0?-dy:dy);
    uint64_t remaining=(uint64_t)INT64_MAX-a;
    if(x>remaining/width)return false;
    remaining-=x*width;
    return y<=remaining/height;
}

bool fzx_model_render(const fzx_model_view_t *view,const fzx_material_texture_t *textures,unsigned count,
                      const uint8_t body[3],const uint8_t fog[3],fzx_model_image_t *image)
{
    if(!body||!fog||!valid(view,textures,count,image))return false;
    const size_t pixels=(size_t)image->width*image->height;
    memset(image->pixels,0,pixels*sizeof(*image->pixels));
    memset(image->depth,0,pixels*sizeof(*image->depth));
    image->tested_pixels=image->drawn_pixels=image->drawn_triangles=0;
    for(unsigned triangle=0;triangle<view->triangle_count;++triangle){
        const fzx_model_triangle_t *tri=&view->triangles[triangle];
        const fzx_model_vertex_t *v[3]={&view->vertices[tri->vertex[0]],&view->vertices[tri->vertex[1]],&view->vertices[tri->vertex[2]]};
        int32_t area=edge(v[0],v[1],v[2]->x_q4,v[2]->y_q4);
        /* The probe keeps both windings so views can diagnose source topology.
         * Depth handles self occlusion. Runtime backface policy is a later gate. */
        if(!area)continue;
        if(area<0){const fzx_model_vertex_t *swap=v[1];v[1]=v[2];v[2]=swap;area=-area;}
        int left=minimum(v[0]->x_q4,v[1]->x_q4,v[2]->x_q4)/16;
        int right=maximum(v[0]->x_q4,v[1]->x_q4,v[2]->x_q4)/16;
        int top=minimum(v[0]->y_q4,v[1]->y_q4,v[2]->y_q4)/16;
        int bottom=maximum(v[0]->y_q4,v[1]->y_q4,v[2]->y_q4)/16;
        if(left<0)left=0;
        if(top<0)top=0;
        if(right>=(int)image->width)right=image->width-1;
        if(bottom>=(int)image->height)bottom=image->height-1;
        if(left>right||top>bottom)continue;
        int32_t e[3],dx[3],dy[3],a[3][7];
        bool inclusive[3];
        for(unsigned j=0;j<3;++j){
            const fzx_model_vertex_t *p=v[(j+1)%3],*q=v[(j+2)%3];
            e[j]=edge(p,q,left*16+8,top*16+8);
            dx[j]=-(q->y_q4-p->y_q4)*16;dy[j]=(q->x_q4-p->x_q4)*16;
            inclusive[j]=q->y_q4<p->y_q4||(q->y_q4==p->y_q4&&q->x_q4>p->x_q4);
            attributes(v[j],a[j]);
        }
        int64_t row[7],sx[7],sy[7];
        for(unsigned k=0;k<7;++k){
            row[k]=sx[k]=sy[k]=0;
            for(unsigned j=0;j<3;++j){
                row[k]+=(int64_t)a[j][k]*e[j];sx[k]+=(int64_t)a[j][k]*dx[j];sy[k]+=(int64_t)a[j][k]*dy[j];
            }
            if(!plane_q8(row[k],area,&row[k])||!plane_q8(sx[k],area,&sx[k])||!plane_q8(sy[k],area,&sy[k])||
               !plane_span_valid(row[k],sx[k],sy[k],(unsigned)(right-left+1),(unsigned)(bottom-top+1)))return false;
        }
        fzx_material_combine_t material=view->materials[tri->material].combine;
        if(view->materials[tri->material].body_environment)memcpy(material.environment,body,3);
        const fzx_material_texture_t *texture=&textures[view->materials[tri->material].texture];
        ++image->drawn_triangles;
        for(int y=top;y<=bottom;++y){
            int32_t at[3]={e[0],e[1],e[2]};int64_t value[7];memcpy(value,row,sizeof(value));
            for(int x=left;x<=right;++x){
                ++image->tested_pixels;
                if((at[0]>0||(at[0]==0&&inclusive[0]))&&(at[1]>0||(at[1]==0&&inclusive[1]))&&
                   (at[2]>0||(at[2]==0&&inclusive[2]))){
                    const unsigned offset=(unsigned)y*image->width+(unsigned)x;
                    const int32_t z=(int32_t)value[0];
                    if(z>0&&(uint32_t)z>=image->depth[offset]){
                        const int32_t u=divide(value[1],z),w=divide(value[2],z);
                        uint16_t channels[4];
                        for(unsigned c=0;c<4;++c){
                            int32_t channel=divide(value[c+3],z);
                            if(channel<0)channel=0;
                            if(channel>65535)channel=65535;
                            channels[c]=(uint16_t)channel;
                        }
                        const fzx_material_color_t shade={channels[0],channels[1],channels[2],channels[3]};
                        const fzx_material_color_t texel=fzx_material_sample(texture,u,w);
                        image->pixels[offset]=fzx_material_pixel(&material,texel,shade,fog);
                        image->depth[offset]=(uint32_t)z;++image->drawn_pixels;
                    }
                }
                for(unsigned j=0;j<3;++j)at[j]+=dx[j];
                for(unsigned k=0;k<7;++k)value[k]+=sx[k];
            }
            for(unsigned j=0;j<3;++j)e[j]+=dy[j];
            for(unsigned k=0;k<7;++k)row[k]+=sy[k];
        }
    }
    return true;
}
