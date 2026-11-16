/* Synthetic native-material differential driver. The GLSL filter body is
 * extracted at test time from the pinned upstream; no retail art is used. */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
extern "C" {
#include "fzx_material.h"
}
struct vec2 {
    float x,y;
    explicit vec2(float a):x(a),y(a){}
    vec2(float a,float b):x(a),y(b){}
    void operator-=(float a){x-=a;y-=a;}
};
static vec2 operator-(vec2 a,vec2 b){return vec2(a.x-b.x,a.y-b.y);}
static vec2 operator*(vec2 a,vec2 b){return vec2(a.x*b.x,a.y*b.y);}
static vec2 operator/(vec2 a,vec2 b){return vec2(a.x/b.x,a.y/b.y);}
static vec2 fract(vec2 a){return vec2(a.x-std::floor(a.x),a.y-std::floor(a.y));}
static float step(float e,float a){return a>=e?1.0f:0.0f;}
static float sign(float a){return float((a>0)-(a<0));}
struct vec4 {float v[4];};
static vec4 operator+(vec4 a,vec4 b){vec4 c{};for(int i=0;i<4;++i)c.v[i]=a.v[i]+b.v[i];return c;}
static vec4 operator-(vec4 a,vec4 b){vec4 c{};for(int i=0;i<4;++i)c.v[i]=a.v[i]-b.v[i];return c;}
static vec4 operator*(float a,vec4 b){for(float &c:b.v)c*=a;return b;}
using sampler2D=const fzx_material_texture_t*;
static int tap(int x,int n,int mode)
{
    if(mode&2)return std::max(0,std::min(n-1,x));
    const int size=(mode&1)?2*n:n;
    while(x<0)x+=size;
    while(x>=size)x-=size;
    return ((mode&1)&&x>=n)?2*n-1-x:x;
}
static vec4 oracle_texture(sampler2D t,vec2 uv)
{
    const int x=tap(int(std::floor(uv.x*t->width)),t->width,t->s_mode);
    const int y=tap(int(std::floor(uv.y*t->height)),t->height,t->t_mode);
    vec4 c{};
    if(t->format==FZX_TEX_RGBA16){
        const int p=(y*t->width+x)*2;
        const int w=t->data[p]*256+t->data[p+1];
        c={{float(((w>>11)&31)*255/31),float(((w>>6)&31)*255/31),float(((w>>1)&31)*255/31),float((w&1)*255)}};
    }else{
        const int p=t->data[y*((t->width+1)/2)+x/2];
        const int n=(x%2?p:p>>4)&15;
        const float value=float(t->format==FZX_TEX_I4?n*17:(n>>1)*36);
        c={{value,value,value,t->format==FZX_TEX_I4?value:float((n&1)*255)}};
    }
    for(float &v:c.v)v/=255.0f;
    return c;
}
#define TEX_OFFSET(off) oracle_texture(tex,texCoord-off/texSize)
#define in
using std::abs;
#include "material_oracle.inc"
#undef in

static uint32_t rng=0x682e13ad;
static uint32_t random_word(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static float shifted(float x,int shift){return shift<=10?x/float(1u<<shift):x*float(1u<<(16-shift));}
static unsigned comparisons;
static void check(bool condition,const char *what)
{
    ++comparisons;
    if(!condition){std::fprintf(stderr,"material mismatch: %s at %u\n",what,comparisons);std::exit(1);}
}

int main()
{
    uint8_t bytes[128];
    for(auto &b:bytes)b=uint8_t(random_word());
    for(int format=0;format<3;++format)for(int sm=0;sm<4;++sm)for(int tm=0;tm<4;++tm){
        fzx_material_texture_t t{bytes,uint32_t(format==0?128:32),8,8,uint8_t(format),uint8_t(sm),uint8_t(tm),3,3,0,0};
        check(fzx_material_texture_valid(&t),"fixture validity");
        for(unsigned i=0;i<3000;++i){
            const int u=int(random_word()%12289)-6144,v=int(random_word()%12289)-6144;
            t.s_shift=uint8_t(i%16);t.t_shift=uint8_t((i/16)%16);
            float s=shifted(float(u)/256.0f,t.s_shift),r=shifted(float(v)/256.0f,t.t_shift);
            /* The native API quantizes shifted coordinates to Q24.8. Its
             * caller supplies this exact bounded convention to the oracle. */
            s=std::floor(s*256.0f)/256.0f;r=std::floor(r*256.0f)/256.0f;
            if(sm&2)s=std::max(0.0f,std::min(7.0f,s));
            if(tm&2)r=std::max(0.0f,std::min(7.0f,r));
            const vec4 expected=filter3point(&t,vec2((s+0.5f)/8,(r+0.5f)/8),vec2(8));
            const auto actual=fzx_material_sample(&t,u,v);
            const uint16_t c[4]={actual.r,actual.g,actual.b,actual.a};
            for(int channel=0;channel<4;++channel)
                check(std::fabs(double(c[channel])-double(expected.v[channel])*65535.0)<=1.1,"source three-point filter");
        }
        ++t.size;check(!fzx_material_texture_valid(&t),"wrong texture size rejected");
    }
    for(int tc=-32768;tc<32768;++tc){
        const int expected=int(std::floor(double(tc)*65535.0/65536.0))*8;
        check(fzx_material_vertex_uv(int16_t(tc),65535)==expected,"source signed UV scaling");
    }
    for(unsigned i=0;i<100000;++i){
        fzx_material_combine_t m{};
        uint8_t fog[3];
        for(int c=0;c<3;++c){m.primitive[c]=uint8_t(random_word());m.environment[c]=uint8_t(random_word());fog[c]=uint8_t(random_word());}
        m.texel_alpha=(i%2)!=0;
        fzx_material_color_t t{uint16_t(random_word()),uint16_t(random_word()),uint16_t(random_word()),uint16_t(random_word())};
        fzx_material_color_t s{uint16_t(random_word()),uint16_t(random_word()),uint16_t(random_word()),uint16_t(random_word())};
        const uint16_t tex[3]={t.r,t.g,t.b},light[3]={s.r,s.g,s.b};
        const auto actual=fzx_material_shade(&m,t,s,fog);
        const uint16_t out[3]={actual.r,actual.g,actual.b};
        const uint16_t pixel=fzx_material_pixel(&m,t,s,fog);
        check((pixel&0x8000)!=0,"reflection is not transparency");
        for(int c=0;c<3;++c){
            const double T=tex[c]/65535.0,E=m.environment[c]/255.0,P=m.primitive[c]/255.0;
            const double material=m.texel_alpha?(T-E)*(t.a/65535.0)+E:(P-E)*T+E;
            const double shaded=material*light[c]/65535.0;
            const double expected=shaded*(1.0-s.a/65535.0)+(fog[c]/255.0)*(s.a/65535.0);
            /* Each UNORM16 multiply rounds by <=0.5; the two mixes plus
             * modulation give an analytical bound of 2.5 native units. */
            check(std::fabs(out[c]-expected*65535.0)<=2.5,"source combiner and reflection");
            const unsigned byte=(unsigned(out[c])+128)/257;
            check(((pixel>>(c*5))&31)==(byte>>3),"native color packing");
        }
    }
    std::printf("material comparisons=%u PASS\n",comparisons);
}
