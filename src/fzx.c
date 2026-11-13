/* F-Zero X Saturn engineering foundation. MIT for new implementation.
 * The spline polynomial/tangent and segment-bank construction are adapted
 * from the CC0 F-Zero X decompilation pinned by G-Diffuser. See NOTICE.md.
 * This is a road-ribbon renderer and prototype driving model, not a port of
 * the original race simulation, N64 renderer, materials or game logic. */
#include "fzx.h"
#include <limits.h>

#include "demo_course.h"
#if defined(FZX_USE_IMPORTED_COURSE) && FZX_USE_IMPORTED_COURSE
#include "../generated/course.h"
#endif

enum { SAMPLES = 8, MAX_RINGS = FZX_MAX_POINTS * SAMPLES,
       VISIBLE_RINGS = 72, RING_VERTS = 4, FOCAL = 180, CLIP_CAP = 12 };
#define FX(N) ((sh2z_fx_t)((N) * 65536))
#define RGB(R,G,B) ((uint16_t)(0x8000u | (R) | ((G)<<5) | ((B)<<10)))

typedef struct {
    sh2z_vec3_t center, forward, left, up;
    sh2z_fx_t width_left, width_right, length;
} ring_t;

static fzx_course_t active_course, upload_course;
static ring_t rings[MAX_RINGS];
static sh2z_vec3_t track_mesh[MAX_RINGS * RING_VERTS];
static sh2z_vec3_t transformed[MAX_RINGS * RING_VERTS];
static sh2z_vec3_t point_ups[FZX_MAX_POINTS];
static sh2z_mesh_view_t mesh_view;
static fzx_frame_t frame;
static fzx_poly_t sort_scratch[FZX_MAX_POLYGONS];
static uint32_t ring_count, current_ring, laps, imported, use_prepared = 1;
static sh2z_fx_t travel, speed, lateral;
static bool initialized;

static sh2z_vec3_t
scaled_add(sh2z_vec3_t a, sh2z_vec3_t b, sh2z_fx_t scale)
{
    return sh2z_vec3_add(a, sh2z_vec3_scale(b, scale));
}

bool
fzx_validate_course(const fzx_course_t *c)
{
    if (!c || c->count < 4 || c->count > FZX_MAX_POINTS) return false;
    for (uint32_t i = 0; i < c->count; ++i) {
        const fzx_control_point_t *p = &c->points[i];
        const fzx_control_point_t *n = &c->points[(i+1) % c->count];
        /* Bounded world coordinates keep the view transform/clip math well
         * inside Q16.16. Imports outside this first renderer's range fail. */
        if (p->position.x < -FX(4096) || p->position.x > FX(4096) ||
            p->position.y < -FX(4096) || p->position.y > FX(4096) ||
            p->position.z < -FX(4096) || p->position.z > FX(4096) ||
            p->radius_left < SH2Z_ONE/4 || p->radius_left > FX(256) ||
            p->radius_right < SH2Z_ONE/4 || p->radius_right > FX(256) ||
            p->tension < 0 || p->tension > FX(2) || p->reserved != 0)
            return false;
        if (sh2z_vec3_length(sh2z_vec3_sub(n->position,p->position)) < SH2Z_ONE)
            return false;
    }
    return true;
}

static sh2z_vec3_t
weighted4(sh2z_vec3_t a, sh2z_vec3_t b, sh2z_vec3_t c, sh2z_vec3_t d,
          sh2z_vec4_t weights)
{
    /* One wide dot/round per component, preserving large cancelling terms. */
    sh2z_vec3_t result = {
        sh2z_vec4_dot((sh2z_vec4_t){a.x,b.x,c.x,d.x},weights),
        sh2z_vec4_dot((sh2z_vec4_t){a.y,b.y,c.y,d.y},weights),
        sh2z_vec4_dot((sh2z_vec4_t){a.z,b.z,c.z,d.z},weights)
    };
    return result;
}

bool
fzx_spline_sample(const fzx_course_t *c, uint32_t segment, sh2z_fx_t t,
                  sh2z_vec3_t *position, sh2z_vec3_t *tangent)
{
    if (!c || c->count < 4 || c->count > FZX_MAX_POINTS ||
        segment >= c->count || t < 0 || t > SH2Z_ONE) return false;
    const sh2z_vec3_t p0 = c->points[(segment+c->count-1)%c->count].position;
    const sh2z_vec3_t p1 = c->points[segment].position;
    const sh2z_vec3_t p2 = c->points[(segment+1)%c->count].position;
    const sh2z_vec3_t p3 = c->points[(segment+2)%c->count].position;
    const sh2z_fx_t a = c->points[segment].tension;
    if (a < 0 || a > FX(2)) return false;
    const sh2z_fx_t tt = sh2z_mul(t,t), ttt = sh2z_mul(tt,t);
    if (position) {
        const sh2z_vec4_t w = {
            sh2z_mul(a, 2*tt-t-ttt),
            sh2z_mul(FX(2)-a,ttt)+sh2z_mul(a-FX(3),tt)+SH2Z_ONE,
            sh2z_mul(a-FX(2),ttt)+sh2z_mul(FX(3)-2*a,tt)+sh2z_mul(a,t),
            sh2z_mul(a,ttt-tt)
        };
        *position = weighted4(p0,p1,p2,p3,w);
    }
    if (tangent) {
        const sh2z_vec4_t w = {
            sh2z_mul(a,4*t-SH2Z_ONE-3*tt),
            sh2z_mul(FX(6)-3*a,tt)+sh2z_mul(2*a-FX(6),t),
            sh2z_mul(3*a-FX(6),tt)+sh2z_mul(FX(6)-4*a,t)+a,
            sh2z_mul(a,3*tt-2*t)
        };
        *tangent = weighted4(p0,p1,p2,p3,w);
    }
    return true;
}

static sh2z_vec3_t
unit_or(sh2z_vec3_t v, sh2z_vec3_t fallback)
{
    if (sh2z_vec3_length(v) < SH2Z_ONE/256) return fallback;
    return sh2z_vec3_normalize(v);
}

static sh2z_vec3_t
control_up(const fzx_course_t *c,uint32_t i)
{
    sh2z_vec3_t forward;
    fzx_spline_sample(c,i,0,0,&forward);
    forward=unit_or(forward,(sh2z_vec3_t){0,0,SH2Z_ONE});
    sh2z_vec3_t right={forward.z,0,sh2z_neg(forward.x)};
    right=unit_or(right,(sh2z_vec3_t){0,0,forward.y});
    const sh2z_vec3_t up=sh2z_vec3_normalize(sh2z_vec3_cross(forward,right));
    sh2z_fx_t sine,cosine;
    sh2z_sincos(c->points[i].bank,&sine,&cosine);
    return sh2z_vec3_sub(sh2z_vec3_scale(up,cosine),sh2z_vec3_scale(right,sine));
}

static bool
course_geometry_valid(const fzx_course_t *c)
{
    if (!fzx_validate_course(c)) return false;
    /* Validate the complete sampled curve before changing any live state.
     * This makes rejected browser uploads transactional. */
    for (uint32_t cp=0;cp<c->count;++cp) {
        sh2z_vec3_t positions[SAMPLES+1],tangents[SAMPLES+1];
        sh2z_fx_t lengths[SAMPLES];
        int64_t total=0,done=0;
        for(uint32_t j=0;j<=SAMPLES;++j) {
            fzx_spline_sample(c,cp,(sh2z_fx_t)(j*SH2Z_ONE/SAMPLES),
                              &positions[j],&tangents[j]);
            if(sh2z_vec3_length(tangents[j])<SH2Z_ONE/32) return false;
        }
        for(uint32_t j=0;j<SAMPLES;++j) {
            lengths[j]=sh2z_vec3_length(sh2z_vec3_sub(positions[j+1],positions[j]));
            if(lengths[j]<SH2Z_ONE/32) return false;
            total+=lengths[j];
        }
        const sh2z_vec3_t up0=control_up(c,cp),up1=control_up(c,(cp+1)%c->count);
        for(uint32_t j=0;j<SAMPLES;++j) {
            const sh2z_fx_t t=(sh2z_fx_t)((done*65536)/total);
            const sh2z_vec3_t up=sh2z_vec3_lerp(up0,up1,t);
            const sh2z_vec3_t forward=sh2z_vec3_normalize(tangents[j]);
            /* Opposing banks can cancel the interpolated up vector. This
             * first ribbon renderer rejects such bases instead of producing
             * a zero camera or silently flattening a loop/twist. */
            if(sh2z_vec3_length(sh2z_vec3_cross(up,forward))<SH2Z_ONE/256)
                return false;
            done+=lengths[j];
        }
    }
    return true;
}

static void
build_track(void)
{
    ring_count=active_course.count*SAMPLES;
    for (uint32_t i=0;i<active_course.count;++i)
        point_ups[i]=control_up(&active_course,i);
    for (uint32_t i=0;i<ring_count;++i) {
        ring_t *r=&rings[i];
        fzx_spline_sample(&active_course,i/SAMPLES,
                          (sh2z_fx_t)((i%SAMPLES)*SH2Z_ONE/SAMPLES),
                          &r->center,&r->forward);
        r->forward=sh2z_vec3_normalize(r->forward);
    }
    for (uint32_t i=0;i<ring_count;++i)
        rings[i].length=sh2z_vec3_length(sh2z_vec3_sub(rings[(i+1)%ring_count].center,
                                                     rings[i].center));
    for (uint32_t cp=0;cp<active_course.count;++cp) {
        const uint32_t next=(cp+1)%active_course.count;
        int64_t total=0,done=0;
        for (uint32_t j=0;j<SAMPLES;++j) total+=rings[cp*SAMPLES+j].length;
        for (uint32_t j=0;j<SAMPLES;++j) {
            const uint32_t i=cp*SAMPLES+j;
            ring_t *r=&rings[i];
            /* Polyline arc length approximates upstream's integrated length.
             * The same fraction drives width and up-vector interpolation. */
            const sh2z_fx_t t=(sh2z_fx_t)((done*65536)/total);
            const sh2z_vec3_t up=sh2z_vec3_lerp(point_ups[cp],point_ups[next],t);
            r->left=unit_or(sh2z_vec3_cross(up,r->forward),
                           (sh2z_vec3_t){SH2Z_ONE,0,0});
            r->up=sh2z_vec3_normalize(sh2z_vec3_cross(r->forward,r->left));
            r->width_left=sh2z_lerp(active_course.points[cp].radius_left,
                                   active_course.points[next].radius_left,t);
            r->width_right=sh2z_lerp(active_course.points[cp].radius_right,
                                    active_course.points[next].radius_right,t);
            track_mesh[i*4]=scaled_add(r->center,r->left,r->width_left);
            track_mesh[i*4+1]=scaled_add(r->center,r->left,-r->width_right);
            track_mesh[i*4+2]=scaled_add(track_mesh[i*4],r->up,SH2Z_ONE);
            track_mesh[i*4+3]=scaled_add(track_mesh[i*4+1],r->up,SH2Z_ONE);
            done+=r->length;
        }
    }
    (void)sh2z_mesh_view_init(&mesh_view,track_mesh,ring_count*4u);
    current_ring=0; travel=0; speed=0; lateral=0; laps=1;
}

FZX_EXPORT void
fzx_init(void)
{
    use_prepared=1;
#if defined(FZX_USE_IMPORTED_COURSE) && FZX_USE_IMPORTED_COURSE
    if (course_geometry_valid(&fzx_course_data)) {
        active_course=fzx_course_data; imported=1;
    } else {
        active_course=fzx_demo_course; imported=0;
    }
#else
    active_course=fzx_demo_course; imported=0;
#endif
    build_track(); initialized=true;
}

FZX_EXPORT fzx_course_t *fzx_course_buffer(void) { return &upload_course; }

FZX_EXPORT int
fzx_activate_course(void)
{
    if (!course_geometry_valid(&upload_course)) return 0;
    active_course=upload_course; imported=1;
    build_track(); initialized=true;
    return 1;
}

FZX_EXPORT void
fzx_use_demo(void)
{
    active_course=fzx_demo_course; imported=0;
    build_track(); initialized=true;
}

FZX_EXPORT void fzx_set_transform_mode(uint32_t prepared) { use_prepared=!!prepared; }

FZX_EXPORT void
fzx_step(uint32_t buttons)
{
    if (!initialized) fzx_init();
    if (buttons&FZX_RESET) { current_ring=0; travel=0; speed=0; lateral=0; laps=1; return; }
    if (buttons&FZX_ACCEL) speed=sh2z_min(speed+983,FX(3)+SH2Z_HALF);
    else speed=sh2z_max(speed-131,0);
    if (buttons&FZX_BRAKE) speed=sh2z_max(speed-3277,0);
    const sh2z_fx_t steer=SH2Z_ONE/8+speed/5;
    if (buttons&FZX_LEFT) lateral=sh2z_add(lateral,steer);
    if (buttons&FZX_RIGHT) lateral=sh2z_sub(lateral,steer);
    const sh2z_fx_t lo=-sh2z_max(rings[current_ring].width_right-SH2Z_ONE,0);
    const sh2z_fx_t hi=sh2z_max(rings[current_ring].width_left-SH2Z_ONE,0);
    if (lateral<lo || lateral>hi) { lateral=sh2z_clamp(lateral,lo,hi); speed-=speed/4; }
    travel=sh2z_add(travel,speed);
    for (uint32_t n=0;n<ring_count && travel>=rings[current_ring].length;++n) {
        travel-=rings[current_ring].length;
        if (++current_ring==ring_count) { current_ring=0; if(laps<UINT32_MAX) ++laps; }
    }
}

static int64_t
clip_distance(sh2z_vec3_t v, unsigned plane)
{
    switch(plane) {
    case 0: return (int64_t)v.z-SH2Z_ONE;
    case 1: return (int64_t)FX(480)-v.z;
    case 2: return (int64_t)v.z*160+(int64_t)v.x*FOCAL;
    case 3: return (int64_t)v.z*160-(int64_t)v.x*FOCAL;
    case 4: return (int64_t)v.z*112+(int64_t)v.y*FOCAL;
    default:return (int64_t)v.z*112-(int64_t)v.y*FOCAL;
    }
}

static void
emit_polygon(const sh2z_vec3_t *v,uint32_t n,uint16_t color)
{
    if(frame.count>=FZX_MAX_POLYGONS) return;
    fzx_poly_t *p=&frame.polys[frame.count];
    p->color=color; p->flags=0; p->depth=0;
    for(uint32_t i=0;i<4;++i) {
        const sh2z_vec3_t q=v[i<n?i:n-1];
        const int32_t x=160+(int32_t)(((int64_t)q.x*FOCAL)/q.z);
        const int32_t y=112-(int32_t)(((int64_t)q.y*FOCAL)/q.z);
        p->x[i]=(int16_t)(x<0?0:(x>=FZX_WIDTH?FZX_WIDTH-1:x));
        p->y[i]=(int16_t)(y<0?0:(y>=FZX_HEIGHT?FZX_HEIGHT-1:y));
        p->depth+=q.z/4;
    }
    /* Discard projected zero-area polygons, including clipped slivers. */
    int32_t area=0;
    for(uint32_t i=0;i<4;++i) {
        uint32_t j=(i+1)&3u;
        area+=(int32_t)p->x[i]*p->y[j]-(int32_t)p->x[j]*p->y[i];
    }
    if(area) ++frame.count;
}

static void
draw_quad(sh2z_vec3_t a,sh2z_vec3_t b,sh2z_vec3_t c,sh2z_vec3_t d,uint16_t color)
{
    sh2z_vec3_t buffers[2][CLIP_CAP];
    buffers[0][0]=a; buffers[0][1]=b; buffers[0][2]=c; buffers[0][3]=d;
    uint32_t count=4; unsigned source=0;
    for(unsigned plane=0;plane<6 && count>=3;++plane) {
        const sh2z_vec3_t *in=buffers[source];
        sh2z_vec3_t *out=buffers[source^1u];
        uint32_t written=0;
        sh2z_vec3_t previous=in[count-1];
        int64_t dp=clip_distance(previous,plane);
        for(uint32_t i=0;i<count;++i) {
            sh2z_vec3_t current=in[i];
            int64_t dc=clip_distance(current,plane);
            if((dp<0)!=(dc<0)) {
                const sh2z_fx_t t=(sh2z_fx_t)((dp*65536)/(dp-dc));
                if(written>=CLIP_CAP) return;
                out[written++]=sh2z_vec3_lerp(previous,current,t);
            }
            if(dc>=0) { if(written>=CLIP_CAP) return; out[written++]=current; }
            previous=current;dp=dc;
        }
        count=written;source^=1u;
    }
    if(count<3) return;
    if(count<=4) emit_polygon(buffers[source],count,color);
    else for(uint32_t i=1;i+1<count;++i) {
        const sh2z_vec3_t triangle[3]={buffers[source][0],buffers[source][i],buffers[source][i+1]};
        emit_polygon(triangle,3,color);
    }
}

static void
sort_polygons(void)
{
    /* Stable bottom-up merge avoids quadratic ordering work in dense views. */
    for(uint32_t width=1;width<frame.count;width*=2) {
        for(uint32_t start=0;start<frame.count;start+=2*width) {
            const uint32_t middle=start+width<frame.count?start+width:frame.count;
            const uint32_t end=start+2*width<frame.count?start+2*width:frame.count;
            uint32_t a=start,b=middle;
            for(uint32_t dst=start;dst<end;++dst) {
                if(a<middle && (b==end || frame.polys[a].depth>=frame.polys[b].depth))
                    sort_scratch[dst]=frame.polys[a++];
                else sort_scratch[dst]=frame.polys[b++];
            }
        }
        for(uint32_t i=0;i<frame.count;++i) frame.polys[i]=sort_scratch[i];
    }
}

FZX_EXPORT const fzx_frame_t *
fzx_render(void)
{
    if(!initialized) fzx_init();
    const ring_t *r=&rings[current_ring],*next=&rings[(current_ring+1)%ring_count];
    const sh2z_fx_t progress=sh2z_div(travel,r->length);
    const sh2z_vec3_t forward=unit_or(sh2z_vec3_lerp(r->forward,next->forward,progress),r->forward);
    const sh2z_vec3_t up=unit_or(sh2z_vec3_lerp(r->up,next->up,progress),r->up);
    const sh2z_vec3_t left=sh2z_vec3_normalize(sh2z_vec3_cross(up,forward));
    sh2z_vec3_t car=scaled_add(sh2z_vec3_lerp(r->center,next->center,progress),left,lateral);
    car=scaled_add(car,up,SH2Z_ONE);
    const sh2z_vec3_t camera=scaled_add(scaled_add(car,forward,-FX(12)),up,FX(7));
    const sh2z_vec3_t view_forward=sh2z_vec3_normalize(sh2z_vec3_sub(scaled_add(car,forward,FX(20)),camera));
    const sh2z_vec3_t view_right=sh2z_vec3_normalize(sh2z_vec3_cross(view_forward,up));
    const sh2z_vec3_t view_up=sh2z_vec3_cross(view_right,view_forward);
    const sh2z_mat34_t view={.m={
        {view_right.x,view_right.y,view_right.z,-sh2z_vec3_dot(view_right,camera)},
        {view_up.x,view_up.y,view_up.z,-sh2z_vec3_dot(view_up,camera)},
        {view_forward.x,view_forward.y,view_forward.z,-sh2z_vec3_dot(view_forward,camera)}
    }};
    if(use_prepared) {
        sh2z_transform_plan_t plan;
        (void)sh2z_transform_plan_init(&plan,&mesh_view,&view);
        sh2z_transform_plan_execute(transformed,&plan);
    } else {
        for(uint32_t i=0;i<ring_count*4;++i)
            transformed[i]=sh2z_mat34_transform_point(&view,track_mesh[i]);
    }
    frame.count=0;frame.speed=(uint32_t)(((int64_t)speed*100)/SH2Z_ONE);
    frame.lap=laps;frame.flags=imported?FZX_FRAME_IMPORTED:0;
    const uint32_t visible=ring_count<VISIBLE_RINGS?ring_count:VISIBLE_RINGS;
    /* Include two rings behind the craft so near-plane clipping stays visible. */
    for(uint32_t j=0;j<visible;++j) {
        uint32_t i=(current_ring+ring_count-2+j)%ring_count,n=(i+1)%ring_count;
        const sh2z_vec3_t *a=&transformed[i*4],*b=&transformed[n*4];
        uint16_t road=(i&1u)?RGB(8,9,14):RGB(10,11,17);
        if(i==0) road=RGB(22,23,26);
        draw_quad(a[0],a[1],b[1],b[0],road);
        draw_quad(a[2],a[0],b[0],b[2],(i&1u)?RGB(2,28,25):RGB(1,16,18));
        draw_quad(a[1],a[3],b[3],b[1],(i&1u)?RGB(24,8,29):RGB(13,3,18));
    }
    /* Original placeholder craft. This is not Blue Falcon asset data. */
    const sh2z_vec3_t body[6]={
        scaled_add(car,forward,FX(3)),
        scaled_add(scaled_add(car,forward,-FX(2)),left,FX(2)),
        scaled_add(scaled_add(car,forward,-FX(2)),left,-FX(2)),
        scaled_add(car,up,SH2Z_ONE),
        scaled_add(scaled_add(car,forward,-FX(1)),left,FX(3)),
        scaled_add(scaled_add(car,forward,-FX(1)),left,-FX(3))
    };
    sh2z_vec3_t ship[6];
    sh2z_mat34_transform_points(ship,&view,body,6);
    draw_quad(ship[0],ship[1],ship[3],ship[3],RGB(4,20,30));
    draw_quad(ship[0],ship[3],ship[2],ship[2],RGB(2,11,24));
    draw_quad(ship[1],ship[2],ship[3],ship[3],RGB(1,6,15));
    draw_quad(ship[0],ship[4],ship[1],ship[1],RGB(1,27,28));
    draw_quad(ship[0],ship[2],ship[5],ship[5],RGB(2,15,24));
    sort_polygons();
    return &frame;
}
