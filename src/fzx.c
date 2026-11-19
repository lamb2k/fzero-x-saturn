/* F-Zero X Saturn engineering foundation. MIT for new implementation.
 * The spline polynomial/tangent and segment-bank construction are adapted
 * from the CC0 F-Zero X decompilation pinned by G-Diffuser. See NOTICE.md.
 * This is a road-ribbon renderer and prototype driving model, not a port of
 * the original race simulation, N64 renderer, materials or game logic. */
#include "fzx.h"
#include <limits.h>
#include <string.h>

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
#if !(FZX_NATIVE_CORE_EMPTY && FZX_SOURCE_ROAD_STREAM)
static sh2z_vec3_t track_mesh[MAX_RINGS * RING_VERTS];
#endif
static sh2z_vec3_t transformed[MAX_RINGS * RING_VERTS];
/* Per transformed vertex in the drawn window: bit p of out_codes set when it surely lies outside
 * clip plane p, in_codes all six bits set when it surely lies inside every plane; such a vertex
 * also has its screen position and depth share (screen_x, screen_y, depth4) worked out once. */
static uint8_t out_codes[MAX_RINGS * RING_VERTS], in_codes[MAX_RINGS * RING_VERTS];
static int16_t screen_x[MAX_RINGS * RING_VERTS], screen_y[MAX_RINGS * RING_VERTS];
static int32_t depth4[MAX_RINGS * RING_VERTS];
static sh2z_vec3_t point_ups[FZX_MAX_POINTS];
static sh2z_mesh_view_t mesh_view;
static fzx_frame_t frame;
static fzx_poly_t sort_scratch[FZX_MAX_POLYGONS];
static uint32_t ring_count, current_ring, laps, imported, use_prepared = 1;
static sh2z_fx_t travel, speed, lateral;
static bool initialized;
/* FZX_SIM_ORIG: the pose last handed over, and whether current_ring tracks it. */
static fzx_pose_t pose;
static bool pose_valid, pose_tracked;
#if FZX_SOURCE_CAMERA
static fzx_view_t source_view;
static bool source_view_valid;
void fzx_set_camera(const fzx_view_t *v) {
    if(v) { source_view=*v; source_view_valid=true; }
    else source_view_valid=false;
}
#if FZX_SIMPIPE_DBUF
const fzx_part_t fzx_core_publication_parts[FZX_CORE_PUBLICATION_PARTS]={
    {&pose,sizeof(pose)},{&pose_valid,sizeof(pose_valid)},{&pose_tracked,sizeof(pose_tracked)},
    {&current_ring,sizeof(current_ring)},{&source_view,sizeof(source_view)},{&source_view_valid,sizeof(source_view_valid)}};
#endif
#endif

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
#if !(FZX_NATIVE_CORE_EMPTY && FZX_SOURCE_ROAD_STREAM)
            track_mesh[i*4]=scaled_add(r->center,r->left,r->width_left);
            track_mesh[i*4+1]=scaled_add(r->center,r->left,-r->width_right);
            track_mesh[i*4+2]=scaled_add(track_mesh[i*4],r->up,SH2Z_ONE);
            track_mesh[i*4+3]=scaled_add(track_mesh[i*4+1],r->up,SH2Z_ONE);
#endif
            done+=r->length;
        }
    }
#if !(FZX_NATIVE_CORE_EMPTY && FZX_SOURCE_ROAD_STREAM)
    (void)sh2z_mesh_view_init(&mesh_view,track_mesh,ring_count*4u);
#endif
    current_ring=0; travel=0; speed=0; lateral=0; laps=1; pose_tracked=false;
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
FZX_EXPORT uint32_t fzx_sim_id(void) { return FZX_SIM; }

static int64_t
distance2(sh2z_vec3_t a, sh2z_vec3_t b)
{
    const int64_t x=(int64_t)a.x-b.x, y=(int64_t)a.y-b.y, z=(int64_t)a.z-b.z;
    /* Q16.16 squared, scaled down so a +/-8192-unit course cannot overflow. */
    return (x>>8)*(x>>8)+(y>>8)*(y>>8)+(z>>8)*(z>>8);
}

FZX_EXPORT void
fzx_set_pose(const fzx_pose_t *p)
{
    if (!initialized) fzx_init();
    if (!p) { pose_valid=false; return; }
    pose=*p; pose_valid=true;
    /* The ring nearest the craft: everywhere once, then near the last one
     * (a craft moves far less than 16 rings in one tick). */
    const uint32_t span=pose_tracked && ring_count>32u ? 16u : ring_count;
    const uint32_t first=pose_tracked ? current_ring+ring_count-(span<ring_count?span:0u) : 0u;
    const uint32_t tries=span<ring_count ? 2u*span+1u : ring_count;
    uint32_t best=current_ring; int64_t best_d=INT64_MAX;
    for (uint32_t n=0;n<tries;++n) {
        const uint32_t i=(first+n)%ring_count;
        const int64_t d=distance2(rings[i].center,p->position);
        if (d<best_d) { best_d=d; best=i; }
    }
    /* The craft lies between the nearest ring and its successor or
     * predecessor: draw from the earlier one. */
    const uint32_t prev=(best+ring_count-1)%ring_count;
    const sh2z_vec3_t to=sh2z_vec3_sub(p->position,rings[best].center);
    if (sh2z_vec3_dot(to,rings[best].forward)<0) best=prev;
    current_ring=best; pose_tracked=true;
    speed=0; laps=p->lap;
}

FZX_EXPORT int32_t
fzx_steer_hint(uint32_t lookahead)
{
    if (!initialized || !pose_valid || ring_count==0) return 0;
    const sh2z_vec3_t target=rings[(current_ring+lookahead)%ring_count].center;
    const sh2z_vec3_t forward=unit_or(pose.forward,rings[current_ring].forward);
    const sh2z_vec3_t up=unit_or(pose.up,rings[current_ring].up);
    const sh2z_vec3_t left=unit_or(sh2z_vec3_cross(up,forward),rings[current_ring].left);
    const int64_t x=(int64_t)target.x-pose.position.x,y=(int64_t)target.y-pose.position.y,
                  z=(int64_t)target.z-pose.position.z;
    /* Course units by unit vectors: Q16.16 x Q16.16 >> 16 stays Q16.16. */
    const int64_t lat=(x*left.x+y*left.y+z*left.z)>>16;
    int64_t fwd=(x*forward.x+y*forward.y+z*forward.z)>>16;
    if (fwd<SH2Z_ONE) fwd=SH2Z_ONE;
    /* Three times the bearing's tangent, in stick units, saturating at full lock. */
    int64_t sx=-(lat*63*3)/fwd;
    if (sx>63) sx=63;
    if (sx<-63) sx=-63;
    return (int32_t)sx;
}

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
#if FZX_SOURCE_CAMERA
    if(source_view_valid) {
        const int32_t p[3]={v.x,v.y,v.z};
        return fzx_view_plane(&source_view,p,plane);
    }
#endif
    switch(plane) {
    case 0: return (int64_t)v.z-SH2Z_ONE;
    case 1: return (int64_t)FX(480)-v.z;
    case 2: return (int64_t)v.z*160+(int64_t)v.x*FOCAL;
    case 3: return (int64_t)v.z*160-(int64_t)v.x*FOCAL;
    case 4: return (int64_t)v.z*112+(int64_t)v.y*FOCAL;
    default:return (int64_t)v.z*112-(int64_t)v.y*FOCAL;
    }
}

/* Which side of each clip plane a vertex lies on, from 32-bit sums of its coordinates in units of
 * 256 Q16.16 steps where they fit. Flooring moves a side plane's sum by less than 340 coarse
 * units either way: a coarse sum >= 340 is surely inside, <= -340 surely outside, anything between
 * is left undecided (neither bit), and the exact clipper settles it. Planes 0 and 1 are exact. */
static void
classify(sh2z_vec3_t v,uint8_t *out,uint8_t *in)
{
    uint8_t o=0,n=0;
#if FZX_SOURCE_CAMERA
    if(source_view_valid) {
        for(unsigned p=0;p<6;++p) {
            if(clip_distance(v,p)<0)o|=(uint8_t)(1u<<p);
            else n|=(uint8_t)(1u<<p);
        }
        *out=o;*in=n;return;
    }
#endif
    if(v.z<SH2Z_ONE) o|=1u; else n|=1u;
    if(v.z>FX(480)) o|=2u; else n|=2u;
    const int32_t x=v.x>>8,y=v.y>>8,z=v.z>>8;
    const int32_t lim=1<<22; /* then |z*160| + |x*180| < 2^31 */
    if(x>-lim && x<lim && y>-lim && y<lim && z>-lim && z<lim) {
        const int32_t d[4]={z*160+x*FOCAL,z*160-x*FOCAL,z*112+y*FOCAL,z*112-y*FOCAL};
        for(unsigned k=0;k<4;++k) {
            if(d[k]>=340) n|=(uint8_t)(4u<<k);
            else if(d[k]<=-340) o|=(uint8_t)(4u<<k);
        }
    } else {
        for(unsigned plane=2;plane<6;++plane) {
            if(clip_distance(v,plane)<0) o|=(uint8_t)(1u<<plane); else n|=(uint8_t)(1u<<plane);
        }
    }
    *out=o; *in=n;
}

/* (a * FOCAL) / z truncated, exactly as the 64-bit expression. On the SH-2 (FZX_SH2_DIVU) the
 * on-chip unit divides signed 64 by 32 bits in 39 cycles where the library would take hundreds.
 * With |a| < 4z the quotient is under 4 * FOCAL, far inside 32 bits (a vertex inside the
 * frustum has |a| <= z * 160 / FOCAL). */
static int32_t
project(sh2z_fx_t a, sh2z_fx_t z)
{
    const int64_t n=(int64_t)a*FOCAL;
#if defined(FZX_SH2_DIVU) && FZX_SH2_DIVU
    if(z>=SH2Z_ONE && z<=FX(480) && a<4*z && a>-4*z) {
        volatile uint32_t *const dvsr=(volatile uint32_t *)0xFFFFFF00u;
        volatile uint32_t *const dvdnth=(volatile uint32_t *)0xFFFFFF10u;
        volatile uint32_t *const dvdntl=(volatile uint32_t *)0xFFFFFF14u;
        *dvsr=(uint32_t)z;
        *dvdnth=(uint32_t)((uint64_t)n>>32);
        *dvdntl=(uint32_t)n; /* starts the division */
        return (int32_t)*dvdntl;
    }
#endif
    return (int32_t)(n/z);
}

static void
vertex_screen(sh2z_vec3_t q,int16_t *sx,int16_t *sy)
{
#if FZX_SOURCE_CAMERA
    if(source_view_valid) {
        const int32_t p[3]={q.x,q.y,q.z};int32_t xy[2];
        if(!fzx_view_project(&source_view,p,xy)) {
            frame.flags|=FZX_FRAME_OVERFLOW;*sx=0;*sy=0;return;
        }
        int32_t x=xy[0]/65536,y=xy[1]/65536;
        if(x<source_view.clip[0])x=source_view.clip[0];
        if(x>=source_view.clip[2])x=source_view.clip[2]-1;
        if(y<source_view.clip[1])y=source_view.clip[1];
        if(y>=source_view.clip[3])y=source_view.clip[3]-1;
        *sx=(int16_t)x;*sy=(int16_t)y;return;
    }
#endif
    const int32_t x=160+project(q.x,q.z);
    const int32_t y=112-project(q.y,q.z);
    *sx=(int16_t)(x<0?0:(x>=FZX_WIDTH?FZX_WIDTH-1:x));
    *sy=(int16_t)(y<0?0:(y>=FZX_HEIGHT?FZX_HEIGHT-1:y));
}

/* Keep the polygon at frame.count unless it projected to zero area (clipped slivers). */
static void
commit_polygon(fzx_poly_t *p)
{
    int32_t area=0;
    for(uint32_t i=0;i<4;++i) {
        uint32_t j=(i+1)&3u;
        area+=(int32_t)p->x[i]*p->y[j]-(int32_t)p->x[j]*p->y[i];
    }
    if(area) ++frame.count;
}

static void
emit_polygon(const sh2z_vec3_t *v,uint32_t n,uint16_t color)
{
    if(frame.count>=FZX_MAX_POLYGONS) {
#if FZX_SOURCE_CAMERA
        frame.flags|=FZX_FRAME_OVERFLOW;
#endif
        return;
    }
    fzx_poly_t *p=&frame.polys[frame.count];
    p->color=color; p->flags=0; p->depth=0;
    for(uint32_t i=0;i<4;++i) {
        const sh2z_vec3_t q=v[i<n?i:n-1];
        vertex_screen(q,&p->x[i],&p->y[i]);
        p->depth+=q.z/4;
    }
    commit_polygon(p);
}

/* inside: planes every corner surely lies inside by classify()'s margin (340 * 256 in the
 * plane's units). Points cut from the quad's edges lie on segments between corners, a rounding
 * step or two from exact, so they stay inside those planes too: the pass would keep every
 * point in order, and it is skipped. */
static void
draw_quad_inside(sh2z_vec3_t a,sh2z_vec3_t b,sh2z_vec3_t c,sh2z_vec3_t d,uint16_t color,unsigned inside)
{
#if FZX_SOURCE_CAMERA
    if(source_view_valid) {
        const int32_t in[4][3]={{a.x,a.y,a.z},{b.x,b.y,b.z},{c.x,c.y,c.z},{d.x,d.y,d.z}};
        int32_t out[10][3];
        const int n=fzx_view_clip(&source_view,in,4,out);
        if(n<0) { frame.flags|=FZX_FRAME_OVERFLOW;return; }
        if(n<3)return;
        sh2z_vec3_t vertices[10];
        for(int i=0;i<n;++i)vertices[i]=(sh2z_vec3_t){out[i][0],out[i][1],out[i][2]};
        if(n<=4)emit_polygon(vertices,(uint32_t)n,color);
        else for(int i=1;i+1<n;++i) {
            const sh2z_vec3_t triangle[3]={vertices[0],vertices[i],vertices[i+1]};
            emit_polygon(triangle,3,color);
        }
        return;
    }
#endif
    sh2z_vec3_t buffers[2][CLIP_CAP];
    buffers[0][0]=a; buffers[0][1]=b; buffers[0][2]=c; buffers[0][3]=d;
    uint32_t count=4; unsigned source=0;
    for(unsigned plane=0;plane<6 && count>=3;++plane) {
        if(inside&(1u<<plane)) continue;
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

/* A window quad by its corners' vertex indices. Wholly inside every plane: emitted from the
 * corners' screen positions as the clipper would (it keeps all four corners in order). Surely
 * outside one plane: dropped, as the clipper leaves nothing. Otherwise clipped. */
static void
draw_window_quad(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint16_t color)
{
    if(out_codes[a]&out_codes[b]&out_codes[c]&out_codes[d]) return;
    if((in_codes[a]&in_codes[b]&in_codes[c]&in_codes[d])==0x3Fu) {
        if(frame.count>=FZX_MAX_POLYGONS) {
#if FZX_SOURCE_CAMERA
        frame.flags|=FZX_FRAME_OVERFLOW;
#endif
        return;
    }
        fzx_poly_t *p=&frame.polys[frame.count];
        const uint32_t k[4]={a,b,c,d};
        p->color=color; p->flags=0; p->depth=0;
        for(uint32_t i=0;i<4;++i) {
            p->x[i]=screen_x[k[i]]; p->y[i]=screen_y[k[i]]; p->depth+=depth4[k[i]];
        }
        commit_polygon(p);
        return;
    }
    draw_quad_inside(transformed[a],transformed[b],transformed[c],transformed[d],color,
                     in_codes[a]&in_codes[b]&in_codes[c]&in_codes[d]);
}

static void
sort_polygons(void)
{
    /* Stable bottom-up merge, far to near, over 2-byte indices with the depths alongside; the
     * 24-byte polygons move once at the end. */
    static uint16_t order[2][FZX_MAX_POLYGONS];
    static int32_t keys[FZX_MAX_POLYGONS];
    const uint32_t count=frame.count;
    if(count<2) return;
    for(uint32_t i=0;i<count;++i) { order[0][i]=(uint16_t)i; keys[i]=frame.polys[i].depth; }
    unsigned src=0;
    for(uint32_t width=1;width<count;width*=2) {
        const uint16_t *in=order[src]; uint16_t *out=order[src^1u];
        for(uint32_t start=0;start<count;start+=2*width) {
            const uint32_t middle=start+width<count?start+width:count;
            const uint32_t end=start+2*width<count?start+2*width:count;
            uint32_t a=start,b=middle;
            for(uint32_t dst=start;dst<end;++dst) {
                if(a<middle && (b==end || keys[in[a]]>=keys[in[b]])) out[dst]=in[a++];
                else out[dst]=in[b++];
            }
        }
        src^=1u;
    }
    for(uint32_t i=0;i<count;++i) sort_scratch[i]=frame.polys[order[src][i]];
    memcpy(frame.polys,sort_scratch,count*sizeof(frame.polys[0]));
}

FZX_EXPORT const fzx_frame_t *
fzx_render(void)
{
    if(!initialized) fzx_init();
#if FZX_NATIVE_CORE_EMPTY
    /* Both visible layers are owned by the native source renderers. Preserve
     * the core's public frame header without transforming an undrawn ribbon
     * or retaining its projection and sorting scratch in high work RAM. */
    frame.count=0;
    frame.speed=pose_valid?pose.speed:(uint32_t)(((int64_t)speed*100)/SH2Z_ONE);
    frame.lap=laps;frame.flags=imported?FZX_FRAME_IMPORTED:0;
    return &frame;
#else
    const ring_t *r=&rings[current_ring],*next=&rings[(current_ring+1)%ring_count];
    sh2z_vec3_t forward,up,left,car;
    if (pose_valid) {
        /* The original code's craft: its own position and attitude. */
        forward=unit_or(pose.forward,r->forward);
        up=unit_or(pose.up,r->up);
        left=unit_or(sh2z_vec3_cross(up,forward),r->left);
        up=sh2z_vec3_normalize(sh2z_vec3_cross(forward,left));
        car=pose.position;
    } else {
        const sh2z_fx_t progress=sh2z_div(travel,r->length);
        forward=unit_or(sh2z_vec3_lerp(r->forward,next->forward,progress),r->forward);
        up=unit_or(sh2z_vec3_lerp(r->up,next->up,progress),r->up);
        left=sh2z_vec3_normalize(sh2z_vec3_cross(up,forward));
        car=scaled_add(sh2z_vec3_lerp(r->center,next->center,progress),left,lateral);
        car=scaled_add(car,up,SH2Z_ONE);
    }
    const sh2z_vec3_t camera=scaled_add(scaled_add(car,forward,-FX(12)),up,FX(7));
    const sh2z_vec3_t view_forward=sh2z_vec3_normalize(sh2z_vec3_sub(scaled_add(car,forward,FX(20)),camera));
    const sh2z_vec3_t view_right=sh2z_vec3_normalize(sh2z_vec3_cross(view_forward,up));
    const sh2z_vec3_t view_up=sh2z_vec3_cross(view_right,view_forward);
    const sh2z_mat34_t view={.m={
        {view_right.x,view_right.y,view_right.z,-sh2z_vec3_dot(view_right,camera)},
        {view_up.x,view_up.y,view_up.z,-sh2z_vec3_dot(view_up,camera)},
        {view_forward.x,view_forward.y,view_forward.z,-sh2z_vec3_dot(view_forward,camera)}
    }};
    frame.count=0;
    frame.speed=pose_valid?pose.speed:(uint32_t)(((int64_t)speed*100)/SH2Z_ONE);
    frame.lap=laps;frame.flags=imported?FZX_FRAME_IMPORTED:0;
    uint32_t visible=ring_count<VISIBLE_RINGS?ring_count:VISIBLE_RINGS;
#if FZX_SOURCE_CAMERA
    if(source_view_valid)visible=ring_count;
#endif
#if defined(FZX_SOURCE_ROAD_TILES) && FZX_SOURCE_ROAD_TILES
    /* Source native road replaces only the temporary ribbon. */
    visible=0;
#endif
    /* Include two rings behind the craft so near-plane clipping stays visible. Only the rings
     * drawn are transformed (visible + 1: each strip joins a ring to its successor), in at most
     * two runs where the window wraps past the course's last ring. */
    uint32_t first=(current_ring+ring_count-2)%ring_count;
#if FZX_SOURCE_CAMERA
    if(source_view_valid)first=0;
#endif
    const uint32_t window=visible+1u<ring_count?visible+1u:ring_count;
    const uint32_t run1=first+window<=ring_count?window:ring_count-first;
    sh2z_transform_plan_t plan;
    if(use_prepared) (void)sh2z_transform_plan_init(&plan,&mesh_view,&view);
    for(uint32_t run=0;run<2;++run) {
        const uint32_t start=run?0u:first,count=run?window-run1:run1;
        if(count==0) continue;
#if FZX_SOURCE_CAMERA
        if(source_view_valid) {
            for(uint32_t i=start*4;i<(start+count)*4;++i) {
                const int32_t p[3]={track_mesh[i].x,track_mesh[i].y,track_mesh[i].z};int32_t q[3];
                if(!fzx_view_transform(&source_view,p,q)) {
                    frame.count=0;frame.flags|=FZX_FRAME_OVERFLOW;return &frame;
                }
                transformed[i]=(sh2z_vec3_t){q[0],q[1],q[2]};
            }
        } else
#endif
        if(use_prepared) (void)sh2z_transform_plan_execute_range(transformed+start*4,&plan,start*4,count*4);
        else for(uint32_t i=start*4;i<(start+count)*4;++i)
            transformed[i]=sh2z_mat34_transform_point(&view,track_mesh[i]);
        for(uint32_t i=start*4;i<(start+count)*4;++i) {
            classify(transformed[i],&out_codes[i],&in_codes[i]);
            if(in_codes[i]==0x3Fu) {
                vertex_screen(transformed[i],&screen_x[i],&screen_y[i]);
                depth4[i]=transformed[i].z/4;
            }
        }
    }
    for(uint32_t j=0;j<visible;++j) {
        uint32_t i=(first+j)%ring_count,n=(i+1)%ring_count;
        const uint32_t a=i*4,b=n*4;
        uint16_t road=(i&1u)?RGB(8,9,14):RGB(10,11,17);
        if(i==0) road=RGB(22,23,26);
        draw_window_quad(a+0,a+1,b+1,b+0,road);
        draw_window_quad(a+2,a+0,b+0,b+2,(i&1u)?RGB(2,28,25):RGB(1,16,18));
        draw_window_quad(a+1,a+3,b+3,b+1,(i&1u)?RGB(24,8,29):RGB(13,3,18));
    }
#if !defined(FZX_SOURCE_MACHINE) || !FZX_SOURCE_MACHINE
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
    uint8_t so[6],si[6];
#if FZX_SOURCE_CAMERA
    if(source_view_valid) {
        for(unsigned i=0;i<6;++i) {
            const int32_t p[3]={body[i].x,body[i].y,body[i].z};int32_t q[3];
            if(!fzx_view_transform(&source_view,p,q)) {
                frame.count=0;frame.flags|=FZX_FRAME_OVERFLOW;return &frame;
            }
            ship[i]=(sh2z_vec3_t){q[0],q[1],q[2]};
        }
    } else
#endif
    sh2z_mat34_transform_points(ship,&view,body,6);
    for(uint32_t k=0;k<6;++k) classify(ship[k],&so[k],&si[k]);
    static const uint8_t faces[5][3]={{0,1,3},{0,3,2},{1,2,3},{0,4,1},{0,2,5}};
    static const uint16_t colors[5]={RGB(4,20,30),RGB(2,11,24),RGB(1,6,15),RGB(1,27,28),RGB(2,15,24)};
    for(uint32_t k=0;k<5;++k) {
        const uint8_t a=faces[k][0],b=faces[k][1],c=faces[k][2];
        if(so[a]&so[b]&so[c]) continue; /* surely outside one plane: the clipper leaves nothing */
        draw_quad_inside(ship[a],ship[b],ship[c],ship[c],colors[k],si[a]&si[b]&si[c]);
    }
#endif
    sort_polygons();
    return &frame;
#endif
}
