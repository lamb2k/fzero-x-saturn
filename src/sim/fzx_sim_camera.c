/* Fixed-point Time Attack camera tick. See fzx_sim_camera.h.
 * MIT; see ../../LICENSE.
 * Each function names the camera.c function it mirrors; statement order is
 * kept so the source can be read side by side. */
#include "fzx_sim_camera.h"

#if defined(__sh__)
/* SH-2 image: size over speed for this once-per-tick code (the high-WRAM
 * stack margin); integer results do not depend on it. */
#pragma GCC optimize("Os")
#endif

volatile uint32_t fzx_cam_drop_bits;

/* Integer constants only: no floating-point expression anywhere in this file. */
#define Q12(n) ((int32_t)(n) * 4096)
#define Q16(n) ((int32_t)(n) * 65536)
#define Q12_HALF INT32_C(2048)
#define Q16_HALF INT32_C(32768)
#define Q16_TENTH INT32_C(6553)          /* floor(0.1 * 65536) */

/* Out-of-line wrappers of the K1 inline kernels: the SH-2 image keeps one copy
 * of each (code size: the high-WRAM stack margin), at a call per use. */
#define FZX_SIM_NOINLINE __attribute__((noinline))
/* x * f for a q12/q16 value x and a u30 factor (format of x kept). */
static FZX_SIM_NOINLINE int32_t mulf(int32_t x,int32_t f) { return sh2z_q12_mul_u30(x,f); }
static FZX_SIM_NOINLINE int32_t umul(int32_t a,int32_t b) { return sh2z_u30_mul(a,b); }
static FZX_SIM_NOINLINE int32_t dot3(const int32_t a[3],const int32_t b[3]) { return sh2z_u30_dot3(a,b); }
static FZX_SIM_NOINLINE void cross(int32_t o[3],const int32_t a[3],const int32_t b[3]) { sh2z_u30_cross(o,a,b); }
static inline int32_t lerp_step(int32_t x,int32_t target,int32_t f) { return x+mulf(target-x,f); }
static inline void copy3(int32_t d[3],const int32_t s[3]) { d[0]=s[0];d[1]=s[1];d[2]=s[2]; }
static inline int32_t iabs(int32_t v) { return v<0 ? -v : v; }

/* Math_Round(DEG_TO_FZXANG2(deg)): round half away from zero of
 * deg * 4096 / 360 = deg_q16 / 5760, exactly (divider). */
static int32_t angle_index(int32_t deg_q16) {
    const int32_t a=iabs(deg_q16);
    const int32_t q=sh2z_divu_q(a+2880,5760,0);
    return deg_q16<0 ? -q : q;
}
/* Exact rounded length of a q12 (or u30) vector, same format. */
static int32_t length3(const int32_t v[3]) {
    const uint64_t s=(uint64_t)((int64_t)v[0]*v[0])+(uint64_t)((int64_t)v[1]*v[1])+(uint64_t)((int64_t)v[2]*v[2]);
    const uint32_t r=sh2z_isqrt64_q12(s);
    return r>UINT32_C(0x7fffffff) ? INT32_MAX : (int32_t)r;
}
static uint64_t sumsq3(const int32_t v[3]) {
    return (uint64_t)((int64_t)v[0]*v[0])+(uint64_t)((int64_t)v[1]*v[1])+(uint64_t)((int64_t)v[2]*v[2]);
}
/* p + d * s for a q12 point, u30 direction d and q12 scalar s. */
static inline void madd3(int32_t out[3],const int32_t p[3],const int32_t d[3],int32_t s) {
    out[0]=p[0]+mulf(s,d[0]);out[1]=p[1]+mulf(s,d[1]);out[2]=p[2]+mulf(s,d[2]);
}
/* 3t^2 - 2t^3 for t in u30. */
static int32_t smooth_u30(int32_t t) {
    const int32_t t2=umul(t,t);
    const int32_t t3=umul(t2,t);
    return 3*t2-2*t3;
}

uint32_t fzx_cam_plan(const FzxCam *c,uint32_t state_flags,uint16_t button_pressed,bool paused,
                      bool falling,bool finished,bool retired) {
    (void)state_flags;(void)button_pressed;
    uint32_t need=FZX_CAM_RUN;
    if(paused) return FZX_CAM_FALLBACK;
    switch(c->mode) {
    case FZX_CAM_MODE_RACE_INTRO:
        if(c->timer>=300) return FZX_CAM_FALLBACK;      /* -> SETUP_RACE */
        need|=FZX_CAM_NEED_INTRO;
        break;
    case FZX_CAM_MODE_SETUP_RACE:
        /* cameraReadyForRace -> RACE: source runs the transition tick */
        if(iabs(c->p_target_fov-c->p_fov)<=Q16_HALF && iabs(c->p_target_fcy-c->p_fcy)<=Q16_HALF &&
           iabs(c->fs_target_distance-c->fs_distance)<=Q12_HALF &&
           iabs(c->fs_target_pitch-c->fs_pitch)<=Q16_HALF)
            return FZX_CAM_FALLBACK;
        break;
    case FZX_CAM_MODE_RACE:
        if(falling || finished) return FZX_CAM_FALLBACK; /* Camera_InitMode */
        if(!retired && (button_pressed & 0x0001u)) return FZX_CAM_FALLBACK; /* BTN_CRIGHT: setting change */
        need|=FZX_CAM_NEED_CONTROL;
        if(c->fov_flags & FZX_CAM_FOV_FLAG_1) need|=FZX_CAM_NEED_SPEED;
        break;
    case FZX_CAM_MODE_FALLING_OFF_TRACK:
        need|=FZX_CAM_NEED_SEG_POS;
        break;
    case FZX_CAM_MODE_COURSE_SELECT: case FZX_CAM_MODE_TITLE_DEMO: case FZX_CAM_MODE_FINISHED_SUCCESS:
    case FZX_CAM_MODE_FINISHED_SPECTATE: case FZX_CAM_MODE_FINISHED_LOSER: case FZX_CAM_MODE_ENDING:
        return FZX_CAM_FALLBACK;
    default:
        break;                                           /* no-op modes (RECORDS_ENTRY, ...) */
    }
    switch(c->type) {
    case FZX_CAM_TYPE_FOLLOW_SMOOTH: need|=FZX_CAM_NEED_FOLLOW; break;
    case FZX_CAM_TYPE_AT_EYE: case FZX_CAM_TYPE_ORBIT_AT: case FZX_CAM_TYPE_ORBIT_EYE:
    case FZX_CAM_TYPE_LOCAL_ANCHOR: break;
    default: return FZX_CAM_FALLBACK;
    }
    return need;
}

/* Camera_UpdateRaceIntro (timer < 300 branch). */
static void update_race_intro(FzxCam *c,const FzxCamInput *in) {
    copy3(c->ob_anchor,in->focus_pos);
    c->ob_basis=in->true_basis;
    const int32_t t=sh2z_divu_q(c->timer,300,30);       /* (f32)timer / 300.0f */
    const int32_t s=smooth_u30(t);
    c->ob_target_distance=Q12(1000)-mulf(Q12(880),s);
    c->ob_target_yaw=Q16(180)-mulf(Q16(360),s);
    c->timer++;
}

/* Camera_UpdateRace (no transition, no setting change). */
static void update_race(FzxCam *c,const FzxCamConst *k,const FzxCamInput *in) {
    if(in->allow_look_back) {
        if((in->button_current & 0x0008u) && !in->retired) {   /* BTN_CUP */
            if(++c->look_back>k->look_back_max) c->look_back=k->look_back_max;
        } else {
            if(--c->look_back<0) c->look_back=0;
        }
        c->fs_target_yaw=c->look_back*k->look_back_step;
    }
    if(c->fov_flags & FZX_CAM_FOV_FLAG_1) {
        int32_t fov;
        const int32_t v[3]={in->unk5c_x,0,in->unk5c_z};
        const int32_t speed=length3(v)-k->fov_speed_min;  /* q12 */
        if(speed<0) {
            fov=c->target_fov;
        } else {
            int32_t r=sh2z_divu_q(speed,k->fov_speed_range,30); /* u30, saturates high */
            if(r>SH2Z_U30_ONE) r=SH2Z_U30_ONE;
            fov=c->target_fov+mulf(Q16(120)-c->target_fov,r);
        }
        c->p_target_fov=fov;
        if(!(c->fov_flags & FZX_CAM_FOV_FLAG_2)) c->p_fov=fov;
    }
    if(iabs(c->p_target_fov-c->p_fov)<=Q16_TENTH) c->fov_flags&=~FZX_CAM_FOV_FLAG_2; /* |d| < 0.1 */
}

/* Camera_SettingsUpdateCommon */
static void settings_common(FzxCam *c) {
    for(int i=0;i<3;i++) c->up[i]=lerp_step(c->up[i],c->target_up[i],c->up_lerp);
    c->p_fov=lerp_step(c->p_fov,c->p_target_fov,c->fov_lerp);
    c->p_fcx=lerp_step(c->p_fcx,c->p_target_fcx,c->fcx_lerp);
    c->p_fcy=lerp_step(c->p_fcy,c->p_target_fcy,c->fcy_lerp);
}

/* Camera_SettingsUpdateAtEye */
static void settings_at_eye(FzxCam *c) {
    for(int i=0;i<3;i++) {
        c->p_at[i]=lerp_step(c->p_at[i],c->ae_target_at[i],c->ae_at_lerp);
        c->p_eye[i]=lerp_step(c->p_eye[i],c->ae_target_eye[i],c->ae_eye_lerp);
    }
}

/* Camera_SettingsUpdateOrbit */
static void settings_orbit(FzxCam *c) {
    int32_t s,co,off[3];
    c->ob_distance=lerp_step(c->ob_distance,c->ob_target_distance,c->ob_distance_lerp);
    c->ob_pitch=lerp_step(c->ob_pitch,c->ob_target_pitch,c->ob_pitch_lerp);
    c->ob_yaw=lerp_step(c->ob_yaw,c->ob_target_yaw,c->ob_yaw_lerp);
    c->pitch_index=angle_index(c->ob_pitch);
    sh2z_sincos12_u30((uint32_t)c->pitch_index,&s,&co);
    const int32_t sp3c=mulf(c->ob_distance,s),sp40=mulf(c->ob_distance,co);
    c->yaw_index=angle_index(c->ob_yaw);
    sh2z_sincos12_u30((uint32_t)c->yaw_index,&s,&co);
    const int32_t t0=mulf(sp40,s),t1=mulf(sp40,co);
    for(int i=0;i<3;i++) {
        const int32_t w[3]={t1,t0,sp3c};
        const int32_t b[3]={c->ob_basis.x[i],c->ob_basis.z[i],c->ob_basis.y[i]};
        off[i]=dot3(w,b);                      /* one rounding for the 3-term sum */
    }
    if(c->type==FZX_CAM_TYPE_ORBIT_AT) {
        copy3(c->p_at,c->ob_anchor);
        for(int i=0;i<3;i++) c->p_eye[i]=c->ob_anchor[i]+off[i];
    } else {
        copy3(c->p_eye,c->ob_anchor);
        for(int i=0;i<3;i++) c->p_at[i]=c->ob_anchor[i]+off[i];
    }
}

/* Camera_SettingsUpdateFollowSmooth */
static void settings_follow(FzxCam *c,const FzxCamConst *k,const FzxCamInput *in) {
    int32_t sp98[3],sp8c[3],sp7c[3],sp70[3],s,co;
    c->fs_distance=lerp_step(c->fs_distance,c->fs_target_distance,c->fs_distance_lerp);
    c->fs_pitch=lerp_step(c->fs_pitch,c->fs_target_pitch,c->fs_pitch_lerp);
    c->fs_yaw=lerp_step(c->fs_yaw,c->fs_target_yaw,c->fs_yaw_lerp);
    copy3(c->focus,in->focus_pos);
    int32_t v=dot3(in->focus_vel,in->gravity_up);         /* q12 */
    for(int i=0;i<3;i++) sp98[i]=(c->fs_anchor[i]-c->focus[i])+mulf(v,in->gravity_up[i]);
    /* sqrtf(...) < 0.01f: sum of q12 squares below (0.01 * 4096)^2 = 1677.72 */
    if(sumsq3(sp98)<=UINT64_C(1677)) return;
    sh2z_vec3_normalize_u30(sp98,sp98);
    madd3(c->fs_anchor,c->focus,sp98,c->fs_distance);              /* !gGamePaused (planned) */
    const int32_t t0=dot3(in->focus_vel,c->basis.y);      /* q12 */
    int32_t kk;
    if(c->fs_target_yaw>=Q16(50)) {
        kk=0;
    } else {
        /* (50 - targetYaw) * 0.02 as u30: d_q16 * 2^14 / 50 = (d_q16 << 13) / 25,
         * one truncating division (d_q16 <= 50 * 2^16 keeps the quotient <= 2^30). */
        kk=sh2z_divu_q(Q16(50)-c->fs_target_yaw,25,13);
    }
    for(int i=0;i<3;i++)
        c->p_at[i]=c->focus[i]+mulf(in->focus_vel[i]-mulf(t0,c->basis.y[i]),kk);
    for(int i=0;i<3;i++) sp8c[i]=in->gravity_up[i]-c->racer_up[i];
    const int32_t m=umul(length3(sp8c),INT32_C(214748365));  /* |sp8C| * 0.2 */
    for(int i=0;i<3;i++) {
        c->racer_up[i]+=umul(m,sp8c[i]);
        c->fs_basis.y[i]=c->racer_up[i];
    }
    cross(sp7c,c->fs_basis.y,sp98);
    /* sqrtf(...) < 0.1f: sum of u30 squares below 0.01 * 2^60 */
    if(sumsq3(sp7c)<UINT64_C(11529215046068470)) return;
    sh2z_vec3_normalize_u30(sp7c,sp7c);
    cross(sp70,sp98,sp7c);
    sh2z_vec3_normalize_u30(sp70,sp70);
    c->pitch_index=angle_index(c->fs_pitch);
    sh2z_sincos12_u30((uint32_t)c->pitch_index,&s,&co);
    const int32_t v1=mulf(mulf(c->fs_distance,s),dot3(sp70,in->true_basis.y));
    int32_t spa4=mulf(c->fs_distance,co);
    c->yaw_index=angle_index(c->fs_target_yaw);
    sh2z_sincos12_u30((uint32_t)c->yaw_index,&s,&co);
    const int32_t t1=mulf(spa4,co);
    spa4=mulf(spa4,s);
    for(int i=0;i<3;i++)
        c->p_eye[i]=c->focus[i]+mulf(spa4,sp7c[i])+mulf(t1,sp98[i])+mulf(v1,sp70[i]);
    for(int i=0;i<3;i++) c->fs_basis.x[i]=c->p_at[i]-c->p_eye[i];  /* q12 difference: direction only */
    sh2z_basis30_orthonormalize_forward(&c->fs_basis);
    copy3(c->target_up,c->fs_basis.y);                              /* !gGamePaused (planned) */
    copy3(c->up,c->fs_basis.y);
    c->p_fcx=0;c->p_target_fcx=0;
    c->p_fcy=k->frustum_center_y;c->p_target_fcy=k->frustum_center_y;
}

/* Camera_SettingsUpdateLocalAnchor */
static void settings_local_anchor(FzxCam *c) {
    for(int i=0;i<3;i++) {
        c->la_at[i]=lerp_step(c->la_at[i],c->la_target_at[i],c->la_at_lerp);
        c->la_eye[i]=lerp_step(c->la_eye[i],c->la_target_eye[i],c->la_eye_lerp);
    }
    for(int i=0;i<3;i++) {
        const int32_t b[3]={c->la_basis.z[i],c->la_basis.y[i],c->la_basis.x[i]};
        c->p_at[i]=c->la_anchor[i]+dot3(c->la_at,b);
        c->p_eye[i]=c->la_anchor[i]+dot3(c->la_eye,b);
    }
}

/* Camera_SettingsUpdateRaceIntroStart (mode RACE_INTRO). */
static void settings_race_intro_start(FzxCam *c,const FzxCamConst *k,const FzxCamInput *in) {
    sh2z_basis30_t b;
    int32_t s,co,temp[3];
    const sh2z_basis30_t *tb=&in->true_basis;
    for(int i=0;i<3;i++) b.x[i]=-tb->x[i];          /* 0 - distance * trueBasis.x, normalised */
    sh2z_vec3_normalize_u30(b.x,b.x);
    cross(b.z,tb->y,b.x);
    sh2z_vec3_normalize_u30(b.z,b.z);
    cross(b.y,b.x,b.z);
    sh2z_vec3_normalize_u30(b.y,b.y);
    const int32_t idx=angle_index(k->regular_pitch);
    sh2z_sincos12_u30((uint32_t)idx,&s,&co);
    const int32_t t0=mulf(mulf(k->regular_distance,s),dot3(b.y,tb->y));
    const int32_t t1=mulf(k->regular_distance,co);
    for(int i=0;i<3;i++) temp[i]=in->focus_pos[i]+mulf(t1,b.x[i])+mulf(t0,b.y[i]);
    const int32_t w=smooth_u30(sh2z_divu_q(c->timer-1,300,30));
    for(int i=0;i<3;i++) c->p_eye[i]+=mulf(temp[i]-c->p_eye[i],w);
    for(int i=0;i<3;i++) b.x[i]=c->p_at[i]-c->p_eye[i];
    copy3(b.y,tb->y);
    sh2z_basis30_orthonormalize_forward(&b);
    copy3(c->up,b.y);
    copy3(c->target_up,b.y);
}

/* func_8071315C */
static void update_basis(FzxCam *c) {
    for(int i=0;i<3;i++) c->basis.x[i]=c->at[i]-c->eye[i];
    sh2z_basis30_orthonormalize_forward(&c->basis);
    const int32_t xz[3]={c->basis.x[0],0,c->basis.x[2]};
    int32_t n[3];
    if(sh2z_vec3_normalize_u30(n,xz)) { c->xz_x=n[0]; c->xz_z=n[2]; }
}

/* Camera_UpdateEnemyEnginePan: the Audio_SetEnemyEnginePan argument, q16. */
static void engine_pan(FzxCam *c,const FzxCamInput *in) {
    int32_t d[3];
    for(int i=0;i<3;i++) d[i]=in->nearest_pos[i]-c->eye[i];
    /* var = |d| * (-(z . d) / |d|) = -(z . d), or 0 when |d| == 0 */
    const int32_t v=sumsq3(d) ? -dot3(c->basis.z,d) : 0;  /* q12 */
    c->pan_q16=-1;
    if(v>=Q12(-1000) && v<=Q12(1000)) {
        /* ((v + 1000) / 2000) * 127, q16 */
        c->pan_q16=sh2z_divu_q(v+Q12(1000),Q12(2000),16)*127;
    }
}

static void drop_bits(int32_t *w,unsigned n,unsigned bits) {
    const int32_t mask=~(int32_t)((UINT32_C(1)<<bits)-1u);
    for(unsigned i=0;i<n;i++) w[i]&=mask;
}

void fzx_cam_tick(FzxCam *c,const FzxCamConst *k,const FzxCamInput *in) {
    c->pitch_index=c->yaw_index=-99999;
    switch(c->mode) {
    case FZX_CAM_MODE_RACE_INTRO: update_race_intro(c,in); break;
    case FZX_CAM_MODE_SETUP_RACE: break;                 /* not ready: no change */
    case FZX_CAM_MODE_RACE: update_race(c,k,in); break;
    case FZX_CAM_MODE_FALLING_OFF_TRACK: copy3(c->ae_target_at,in->seg_pos); break;
    default: break;
    }
    /* Camera_SettingsUpdate */
    switch(c->type) {
    case FZX_CAM_TYPE_AT_EYE: settings_at_eye(c); break;
    case FZX_CAM_TYPE_FOLLOW_SMOOTH: settings_follow(c,k,in); break;
    case FZX_CAM_TYPE_ORBIT_AT: case FZX_CAM_TYPE_ORBIT_EYE: settings_orbit(c); break;
    case FZX_CAM_TYPE_LOCAL_ANCHOR: settings_local_anchor(c); break;
    default: break;
    }
    settings_common(c);
    if(c->mode==FZX_CAM_MODE_RACE_INTRO) settings_race_intro_start(c,k,in);
    /* Camera_UpdateFromSettings */
    copy3(c->eye,c->p_eye);
    copy3(c->at,c->p_at);
    copy3(c->basis.y,c->up);
    c->fov=c->p_fov;c->fcx=c->p_fcx;c->fcy=c->p_fcy;
    update_basis(c);
    c->pan_q16=-1;
    if(in->engine_pan) engine_pan(c,in);
    if(fzx_cam_drop_bits) {
        const unsigned b=fzx_cam_drop_bits;
        drop_bits(c->eye,3,b);drop_bits(c->at,3,b);drop_bits(c->p_eye,3,b);drop_bits(c->p_at,3,b);
        drop_bits(&c->basis.x[0],9,b);drop_bits(c->up,3,b);drop_bits(c->target_up,3,b);
        drop_bits(c->fs_anchor,3,b);drop_bits(&c->fs_distance,1,b);drop_bits(&c->fs_pitch,1,b);
        drop_bits(&c->fs_yaw,1,b);drop_bits(c->racer_up,3,b);drop_bits(&c->p_fov,1,b);
        drop_bits(&c->ob_distance,1,b);drop_bits(&c->ob_pitch,1,b);drop_bits(&c->ob_yaw,1,b);
    }
}
