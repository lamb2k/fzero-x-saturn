#include "fzx_machine.h"
#include <limits.h>

static uint32_t modulate(uint32_t value,uint32_t factor)
{
    const uint32_t product=value*factor+32768u;
    return (product+(product>>16))>>16;
}
static uint16_t lit(fzx_material_color_t base,fzx_material_color_t shade,const uint8_t fog[3])
{
    const uint16_t colors[3]={base.r,base.g,base.b},light[3]={shade.r,shade.g,shade.b};
    uint16_t result=0x8000;
    for(unsigned c=0;c<3;++c){
        uint32_t value=modulate(modulate(colors[c],light[c]),65535u-shade.a)+modulate((uint32_t)fog[c]*257u,shade.a);
        if(value>65535)value=65535;
        result|=(uint16_t)((((value+128u)/257u)>>3)<<(c*5));
    }
    return result;
}
bool fzx_machine_light(const fzx_material_color_t *base,unsigned count,
                       const fzx_material_color_t shade[3],const uint8_t fog[3],fzx_machine_palette_t *out)
{
    if(!base||!shade||!fog||!out||!count||count>16)return false;
    fzx_material_color_t anchor={
        (uint16_t)(((uint32_t)shade[0].r+shade[1].r+shade[2].r)/3),
        (uint16_t)(((uint32_t)shade[0].g+shade[1].g+shade[2].g)/3),
        (uint16_t)(((uint32_t)shade[0].b+shade[1].b+shade[2].b)/3),
        (uint16_t)(((uint32_t)shade[0].a+shade[1].a+shade[2].a)/3)};
    uint32_t r=0,g=0,b=0;
    for(unsigned i=0;i<count;++i){out->palette[i]=lit(base[i],anchor,fog);r+=base[i].r;g+=base[i].g;b+=base[i].b;}
    for(unsigned i=count;i<16;++i)out->palette[i]=out->palette[0];
    const fzx_material_color_t representative={(uint16_t)(r/count),(uint16_t)(g/count),(uint16_t)(b/count),65535};
    const uint16_t middle=lit(representative,anchor,fog);
    out->saturated_channels=0;
    for(unsigned i=0;i<3;++i){
        const uint16_t vertex=lit(representative,shade[i],fog);uint16_t correction=0;
        for(unsigned c=0;c<3;++c){
            int value=16+(int)((vertex>>(c*5))&31u)-(int)((middle>>(c*5))&31u);
            if(value<0){value=0;++out->saturated_channels;}
            if(value>31){value=31;++out->saturated_channels;}
            correction|=(uint16_t)((unsigned)value<<(c*5));
        }
        out->gouraud[i]=correction;
    }
    out->gouraud[3]=out->gouraud[2];return true;
}

static int32_t divide(int64_t value,int32_t divisor)
{
#if defined(__sh__)
    volatile int32_t *const dvsr=(volatile int32_t*)0xFFFFFF00u;
    volatile uint32_t *const high=(volatile uint32_t*)0xFFFFFF10u,*const low=(volatile uint32_t*)0xFFFFFF14u;
    *dvsr=divisor;*high=(uint32_t)((uint64_t)value>>32);*low=(uint32_t)value;
    return (int32_t)*low;
#else
    return (int32_t)(value/divisor);
#endif
}
static bool pose_valid(const fzx_machine_pose_t *pose)
{
    if(!pose||pose->distance<513||pose->distance>4096||pose->pivot_y < -1024||pose->pivot_y>1024)return false;
    for(unsigned j=0;j<3;++j){
        if(pose->light[j] < -16384||pose->light[j]>16384||pose->reflection[j] < -16384||pose->reflection[j]>16384)return false;
        for(unsigned k=0;k<3;++k)if(pose->rotation[j][k] < -16384||pose->rotation[j][k]>16384)return false;
    }
    return true;
}
static bool project_position(const int16_t position[3],const fzx_machine_pose_t *pose,fzx_machine_position_t *out)
{
    int32_t p[3];
    for(unsigned j=0;j<3;++j)if(position[j] < -1024||position[j]>1024)return false;
    for(unsigned j=0;j<3;++j){
        int32_t value=0;
        for(unsigned k=0;k<3;++k)value+=((int32_t)position[k]-(k==1?pose->pivot_y:0))*pose->rotation[j][k];
        p[j]=value/64;
    }
    p[2]+=(int32_t)pose->distance*256;
    if(p[2]<513*256)return false;
    const int32_t x=1024+divide((int64_t)p[0]*2560,p[2]),y=1024-divide((int64_t)p[1]*2560,p[2]);
    if(x < -4096||x>8192||y < -4096||y>8192)return false;
    *out=(fzx_machine_position_t){(int16_t)x,(int16_t)y,(uint16_t)divide(INT64_C(1)<<32,p[2])};return true;
}
static fzx_material_color_t project_normal(const int8_t normal[3],const fzx_machine_pose_t *pose,const uint8_t lut[256])
{
    int32_t n[3];
    for(unsigned j=0;j<3;++j){int32_t value=0;for(unsigned k=0;k<3;++k)value+=(int32_t)normal[k]*pose->rotation[j][k];n[j]=value/64;}
    int64_t diffuse=0,reflection=0;
    for(unsigned j=0;j<3;++j){diffuse+=(int64_t)n[j]*pose->light[j];reflection+=(int64_t)n[j]*pose->reflection[j];}
    int32_t shade=100;
    if(diffuse>0)shade+=divide(diffuse*255,127*256*16384);
    if(shade>255)shade=255;
    int32_t index=divide(reflection,256*16384);
    if(index < -128)index=-128;
    if(index>127)index=127;
    return (fzx_material_color_t){(uint16_t)(shade*257),(uint16_t)(shade*257),(uint16_t)(shade*257),(uint16_t)(lut[128+index]*257)};
}
static void store_vertex(fzx_model_vertex_t *out,const fzx_machine_vertex_t *in,
                          fzx_machine_position_t p,fzx_material_color_t shade)
{*out=(fzx_model_vertex_t){p.x_q4,p.y_q4,p.inverse_z,in->uv[0],in->uv[1],shade};}
bool fzx_machine_project(const fzx_machine_vertex_t *vertices,unsigned count,
                         const fzx_machine_pose_t *pose,const uint8_t lut[256],fzx_model_vertex_t *out)
{
    if(!vertices||!lut||!out||!count||count>2048||!pose_valid(pose))return false;
    for(unsigned i=0;i<count;++i){
        fzx_machine_position_t p;if(!project_position(vertices[i].position,pose,&p))return false;
        store_vertex(&out[i],&vertices[i],p,project_normal(vertices[i].normal,pose,lut));
    }
    return true;
}
static bool plan_bounds(const fzx_machine_plan_t *p)
{
    return p&&p->position_sources&&p->normal_sources&&p->position_map&&p->normal_map&&
        p->vertex_count&&p->vertex_count<=2048&&p->position_count&&p->normal_count&&
        p->position_count<=p->vertex_count&&p->normal_count<=p->vertex_count;
}
bool fzx_machine_plan_valid(const fzx_machine_vertex_t *v,const fzx_machine_plan_t *p)
{
    if(!v||!plan_bounds(p))return false;
    for(unsigned i=0;i<p->position_count;++i)if(p->position_sources[i]>=p->vertex_count)return false;
    for(unsigned i=0;i<p->normal_count;++i)if(p->normal_sources[i]>=p->vertex_count)return false;
    for(unsigned i=0;i<p->vertex_count;++i){
        if(p->position_map[i]>=p->position_count||p->normal_map[i]>=p->normal_count)return false;
        const fzx_machine_vertex_t *a=&v[p->position_sources[p->position_map[i]]],*b=&v[p->normal_sources[p->normal_map[i]]];
        for(unsigned j=0;j<3;++j)if(v[i].position[j]!=a->position[j]||v[i].normal[j]!=b->normal[j])return false;
    }
    return true;
}
bool fzx_machine_project_reuse(const fzx_machine_vertex_t *v,const fzx_machine_plan_t *p,
    const fzx_machine_pose_t *pose,const uint8_t lut[256],fzx_machine_position_t *positions,
    fzx_material_color_t *normals,fzx_model_vertex_t *out)
{
    if(!v||!lut||!positions||!normals||!out||!plan_bounds(p)||!pose_valid(pose))return false;
    for(unsigned i=0;i<p->position_count;++i)
        if(!project_position(v[p->position_sources[i]].position,pose,&positions[i]))return false;
    for(unsigned i=0;i<p->normal_count;++i)normals[i]=project_normal(v[p->normal_sources[i]].normal,pose,lut);
    for(unsigned i=0;i<p->vertex_count;++i)store_vertex(&out[i],&v[i],positions[p->position_map[i]],normals[p->normal_map[i]]);
    return true;
}
bool fzx_machine_light_reuse(const fzx_material_color_t *base,unsigned count,
    const fzx_material_color_t shade[3],const uint8_t fog[3],fzx_machine_palette_t *out)
{
    if(!base||!shade||!fog||!out||!count||count>16)return false;
    for(unsigned i=1;i<3;++i)if(shade[i].r!=shade[0].r||shade[i].g!=shade[0].g||shade[i].b!=shade[0].b||shade[i].a!=shade[0].a)
        return fzx_machine_light(base,count,shade,fog,out);
    for(unsigned i=0;i<count;++i)out->palette[i]=lit(base[i],shade[0],fog);
    for(unsigned i=count;i<16;++i)out->palette[i]=out->palette[0];
    for(unsigned i=0;i<4;++i)out->gouraud[i]=0x4210;
    out->saturated_channels=0;return true;
}
