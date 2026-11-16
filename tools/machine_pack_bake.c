/* Offline source-texture charts; output is private, never linked as C arrays. */
#define main unused_rgb_chart_main
#include "vdp1_probe/bake.c"
#undef main
static void word(unsigned x){putchar((int)(x>>8)&255);putchar((int)x&255);}
static bool equal(fzx_material_color_t a,fzx_material_color_t b)
{return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;}
static int32_t nearest_texel(int32_t x){x+=128;return (x/256-(x%256<0))*256;}
static unsigned chart_dimension(double n,unsigned multiple){unsigned d=dimension(n,multiple);return d>16?16:d;}
int main(void){
    const unsigned views[3]={0,6,7};
    for(unsigned lod=0;lod<3;++lod){
        const fzx_model_view_t *view=&fzx_model_views[views[lod]];
        if(!valid(view))return 2;
        word(view->triangle_count);
        for(unsigned k=0;k<view->triangle_count;++k){
            const fzx_model_triangle_t *tri=&view->triangles[k];
            const fzx_model_material_t *m=&view->materials[tri->material];
            const fzx_material_texture_t *tex=&fzx_model_textures[m->texture];
            double u[3],v[3];
            for(unsigned j=0;j<3;++j){const fzx_model_vertex_t *p=&view->vertices[tri->vertex[j]];
                u[j]=fzx_material_vertex_uv(p->u_q5,65535);v[j]=fzx_material_vertex_uv(p->v_q5,65535);}
            unsigned w=chart_dimension(hypot(u[1]-u[0],v[1]-v[0])/256,8);
            unsigned h=chart_dimension(fmax(hypot(u[2]-u[0],v[2]-v[0]),hypot(u[2]-u[1],v[2]-v[1]))/256,2);
            uint8_t indices[256];fzx_material_color_t palette[16];unsigned count=0;
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                double s=(double)x/(w-1),t=(double)y/(h-1),a=(1-t)*(1-s),b=(1-t)*s;
                fzx_material_color_t color=fzx_material_sample(tex,nearest_texel((int32_t)(u[0]*a+u[1]*b+u[2]*t)),nearest_texel((int32_t)(v[0]*a+v[1]*b+v[2]*t)));
                unsigned index=0;while(index<count&&!equal(palette[index],color))++index;
                if(index==count){if(count==16){fprintf(stderr,"chart palette overflow lod=%u triangle=%u\n",lod,k);return 3;}palette[count++]=color;}
                indices[y*w+x]=(uint8_t)index;
            }
            word(w);word(h);word(count);word(k);word(tri->material);
            for(unsigned i=0;i<count;++i){word(palette[i].r);word(palette[i].g);word(palette[i].b);word(palette[i].a);}
            for(unsigned i=0;i<w*h;i+=2)putchar((indices[i]<<4)|indices[i+1]);
        }
    }
    return ferror(stdout)?4:0;
}
