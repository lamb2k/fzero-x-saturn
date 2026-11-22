/* Derive race constants at LOAD from the selected stock identity and the
 * source balance slider. Only the small ROM-dependent endpoints are baked. */
#include "machine_tuning.h"
#include "stock_tuning.h"
#include <string.h>

/* The guests retain their own identity in sessions and records. Their handling
 * uses two stock presets, Black Bull for the Eggmobile and Sonic Phantom for Metal Sonic. */
static unsigned handling_profile(unsigned machine) {
    if (machine == 33u) return 15u;
    if (machine == 34u) return 28u;
    return machine;
}

fzx_machine_grade_t fzx_machine_grade(unsigned machine) {
    machine = handling_profile(machine);
    if (machine >= 30) return (fzx_machine_grade_t){0, '-', '-', '-'};
    const stock_machine_t *m = &stock_machine[machine];
    return (fzx_machine_grade_t){m->weight, 'A'+m->body, 'A'+m->boost, 'A'+m->grip};
}

static float inverse_balance(float x) {
    return x == 0 ? 0 : (1.0f + 0.6899998f) / ((1.0f / x) + 0.6899998f);
}

static int32_t q16(float x) {
    float scaled = x * 65536.0f;
    int32_t whole = (int32_t)scaled;
    float fraction = scaled - whole;
    return whole + (fraction >= 0.5f) - (fraction <= -0.5f);
}

static int32_t q30(float x) { return q16(x * 16384.0f); }

/* The engine force constants in the build's force scale (SH2Z_VD_FORCE_FRAC, vehicle-dynamics-4): x * 2^F is exact in
 * binary32 (a power of two), rounded to the nearest integer with ties away from zero, as q16 does at F = 16. */
static int32_t qf(float x) { return q16(x * (float)(1u << (SH2Z_VD_FORCE_FRAC - 16))); }

bool fzx_machine_tune(FzxPhysCourse *course, unsigned machine, uint32_t slider_bits) {
    machine = handling_profile(machine);
    if (machine >= 30 || slider_bits > 0x3f800000u) return false;
    const stock_machine_t *m = &stock_machine[machine];
    float slider;
    memcpy(&slider, &slider_bits, sizeof(slider));
    float x = slider == 0 ? 0 : 1.0f / (((1.0f + 0.6899998f) / slider) - 0.6899998f);
    float offset = 2.0f*x - 1.0f, curvature = 1.0f - offset*offset;
    float y[9];
    for (unsigned i=0; i<9; ++i) {
        float low=m->curve[i][0], high=m->curve[i][1], delta=high-low;
        y[i]=delta*x+low;
        if (i < 2) y[i] += (0.2f*(low-high))*curvature;
        if (i == 4 || i == 5) y[i] += (0.2f*delta)*curvature;
    }
    float boosted=(stock_boost_force[m->boost]+y[8])/y[2];
    float plated=(stock_boost_force[2]+y[8])/y[2];
    float slew=(0.1f-1.0f)*inverse_balance(inverse_balance(inverse_balance(x)))+1.0f;
    sh2z_vd_vehicle_t *v=&course->vehicle;
    v->force_low=qf(y[1]); v->force_slide=qf(y[0]); v->force_high=qf(y[2]); v->crossover=q16(y[3]);
    v->boost_mul=q16(boosted); v->plate_mul=q16(plated);
    v->slide_max=q16(y[4]); v->slide_decay=q16(y[4]/y[5]); v->slew_base=q16(slew);
    v->turn_free=q30(((m->weight-780.f)*-0.0050000027f)/1560.f+0.054f);
    v->turn_grip=q30(((m->weight-780.f)*-0.004999999f)/1560.f+0.03f);
    /* The reference adds the grip grade before the lower curve endpoint. */
    v->grip_limit=q16(stock_grip_force[m->grip]+(m->curve[6][1]-m->curve[6][0])*x+m->curve[6][0]);
    v->slide_limit=q16(stock_slide_force[m->grip]+(m->curve[7][1]-m->curve[7][0])*x+m->curve[7][0]);
    v->slide_mul=q16(plated-1.0f); v->bump=q16((0.5f-x)*0.5f);
    v->slew_gain=q16(1.0f/(2.0f*y[3])); v->slew_slope=q16(1.0f-slew);
    v->max_energy=q16(stock_health[m->body]); v->boost_cost=q16(stock_health[m->body]*0.0015f);
    v->recharge=q16(stock_health[m->body]*0.008f);
    course->start.energy = v->max_energy;
    return true;
}
