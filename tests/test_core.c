/* Host verification of the Saturn port foundation. Synthetic geometry only.
 * CHECK remains active under -DNDEBUG/Release. Double is used only by the
 * independent Hermite oracle, never by the runtime under test. */
#include "fzx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FX(N) ((sh2z_fx_t)((N) * 65536))
static unsigned checks;

static void
require(int condition, const char *expression, const char *file, int line)
{
    ++checks;
    if (!condition) {
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(C) require(!!(C), #C, __FILE__, __LINE__)

static fzx_course_t
square_course(void)
{
    fzx_course_t result = {0};
    const int xyz[4][3] = {{-40, 0, -40}, {40, 0, -40}, {40, 0, 40}, {-40, 0, 40}};
    result.count = 4;
    for (unsigned i = 0; i < 4; ++i) {
        result.points[i].position = (sh2z_vec3_t){FX(xyz[i][0]), FX(xyz[i][1]), FX(xyz[i][2])};
        result.points[i].radius_left = FX(8);
        result.points[i].radius_right = FX(8);
        result.points[i].tension = SH2Z_HALF;
    }
    return result;
}

static size_t
frame_bytes(const fzx_frame_t *f)
{
    CHECK(f->count <= FZX_MAX_POLYGONS);
    return offsetof(fzx_frame_t, polys) + f->count * sizeof(fzx_poly_t);
}

static void
same_frame(const fzx_frame_t *a, const fzx_frame_t *b)
{
    CHECK(a->count == b->count);
    CHECK(a->speed == b->speed);
    CHECK(a->lap == b->lap);
    CHECK(a->flags == b->flags);
    CHECK(memcmp(a, b, frame_bytes(a)) == 0);
}

static void
check_frame(const fzx_frame_t *f)
{
    CHECK(f != NULL);
    CHECK(f->count > 0 && f->count <= FZX_MAX_POLYGONS);
    CHECK(f->lap >= 1);
    CHECK(f->speed <= 350);
    for (uint32_t i = 0; i < f->count; ++i) {
        const fzx_poly_t *p = &f->polys[i];
        CHECK((p->color & 0x8000u) != 0);
        CHECK(p->depth > 0);
        CHECK(p->flags == 0);
        if (i) CHECK(f->polys[i - 1].depth >= p->depth);
        int area = 0;
        for (unsigned j = 0; j < 4; ++j) {
            CHECK(p->x[j] >= 0 && p->x[j] < FZX_WIDTH);
            CHECK(p->y[j] >= 0 && p->y[j] < FZX_HEIGHT);
            unsigned next = (j + 1) & 3u;
            area += p->x[j] * p->y[next] - p->x[next] * p->y[j];
        }
        CHECK(area != 0);
    }
}

static double
component(sh2z_vec3_t v, unsigned axis)
{
    return (axis == 0 ? v.x : axis == 1 ? v.y : v.z) / 65536.0;
}

static void
test_spline_against_independent_hermite_oracle(void)
{
    fzx_course_t c = square_course();
    /* Unequal spacing, height, and adaptive tensions prevent a constant-tension
     * or straight-segment substitution from accidentally passing this test. */
    c.count = 5;
    const int positions[5][3] = {{-80, 12, 20}, {50, -5, 100}, {190, 30, 40},
                                  {140, 100, -130}, {-80, 25, -80}};
    const sh2z_fx_t tensions[5] = {SH2Z_ONE / 4, SH2Z_HALF, SH2Z_ONE, 3 * SH2Z_HALF, FX(2)};
    for (unsigned i = 0; i < c.count; ++i) {
        c.points[i].position = (sh2z_vec3_t){FX(positions[i][0]), FX(positions[i][1]), FX(positions[i][2])};
        c.points[i].radius_left = c.points[i].radius_right = FX(8);
        c.points[i].tension = tensions[i];
    }
    double max_position_error = 0, max_tangent_error = 0;
    for (unsigned segment = 0; segment < c.count; ++segment) {
        sh2z_vec3_t start, end, tangent;
        CHECK(fzx_spline_sample(&c, segment, 0, &start, &tangent));
        CHECK(memcmp(&start, &c.points[segment].position, sizeof(start)) == 0);
        CHECK(fzx_spline_sample(&c, segment, SH2Z_ONE, &end, &tangent));
        CHECK(memcmp(&end, &c.points[(segment + 1) % c.count].position, sizeof(end)) == 0);
        for (unsigned sample = 0; sample <= 127; ++sample) {
            sh2z_fx_t fixed_t = (sh2z_fx_t)((sample * 65536u) / 127u);
            double t = fixed_t / 65536.0, t2 = t * t, t3 = t2 * t;
            double a = c.points[segment].tension / 65536.0;
            sh2z_vec3_t point, direction;
            CHECK(fzx_spline_sample(&c, segment, fixed_t, &point, &direction));
            for (unsigned axis = 0; axis < 3; ++axis) {
                double p0 = component(c.points[(segment + c.count - 1) % c.count].position, axis);
                double p1 = component(c.points[segment].position, axis);
                double p2 = component(c.points[(segment + 1) % c.count].position, axis);
                double p3 = component(c.points[(segment + 2) % c.count].position, axis);
                /* Cubic Hermite interpolation of endpoint positions/slopes is
                 * algebraically independent of the runtime's four weights. */
                double m0 = a * (p2 - p0), m1 = a * (p3 - p1);
                double oracle_position = (2*t3 - 3*t2 + 1)*p1 + (t3 - 2*t2 + t)*m0 +
                                         (-2*t3 + 3*t2)*p2 + (t3 - t2)*m1;
                double oracle_tangent = (6*t2 - 6*t)*p1 + (3*t2 - 4*t + 1)*m0 +
                                        (-6*t2 + 6*t)*p2 + (3*t2 - 2*t)*m1;
                double pe = fabs(component(point, axis) - oracle_position);
                double te = fabs(component(direction, axis) - oracle_tangent);
                if (pe > max_position_error) max_position_error = pe;
                if (te > max_tangent_error) max_tangent_error = te;
                CHECK(pe < 0.025);
                CHECK(te < 0.025);
            }
        }
    }
    printf("Hermite oracle: position error %.8f, tangent error %.8f (scaled units)\n",
           max_position_error, max_tangent_error);
    CHECK(!fzx_spline_sample(NULL, 0, 0, NULL, NULL));
    CHECK(!fzx_spline_sample(&c, c.count, 0, NULL, NULL));
    CHECK(!fzx_spline_sample(&c, 0, -1, NULL, NULL));
    CHECK(!fzx_spline_sample(&c, 0, SH2Z_ONE + 1, NULL, NULL));
}

static uint32_t
controls(unsigned tick)
{
    uint32_t buttons = (tick % 271u < 230u) ? FZX_ACCEL : 0u;
    unsigned steering = tick % 311u;
    if (steering >= 35u && steering < 75u) buttons |= FZX_LEFT;
    if (steering >= 170u && steering < 210u) buttons |= FZX_RIGHT;
    if (tick % 227u > 204u) buttons |= FZX_BRAKE;
    return buttons;
}

static void
drive(unsigned begin, unsigned end)
{
    for (unsigned tick = begin; tick < end; ++tick) fzx_step(controls(tick));
}

static void
test_controls_reset_and_determinism(void)
{
    fzx_init();
    fzx_use_demo();
    fzx_frame_t initial = *fzx_render();
    CHECK(initial.speed == 0 && initial.lap == 1 && initial.flags == 0);
    CHECK(initial.count > 5); /* More than the five placeholder ship faces. */
    same_frame(&initial, fzx_render()); /* Rendering must never advance time. */
    drive(0, 900);
    fzx_frame_t driven = *fzx_render();
    check_frame(&driven);
    CHECK(memcmp(initial.polys, driven.polys,
                 (initial.count < driven.count ? initial.count : driven.count) * sizeof(fzx_poly_t)) != 0);
    fzx_step(FZX_RESET | FZX_ACCEL);
    same_frame(&initial, fzx_render());
    drive(0, 900);
    same_frame(&driven, fzx_render());
    fzx_step(FZX_RESET);
    for (unsigned i = 0; i < 240; ++i) fzx_step(FZX_ACCEL);
    uint32_t fast_speed = fzx_render()->speed;
    CHECK(fast_speed > 250);
    for (unsigned i = 0; i < 120; ++i) fzx_step(FZX_BRAKE);
    CHECK(fzx_render()->speed == 0);
    fzx_step(FZX_RESET);
    same_frame(&initial, fzx_render());
}

static void
test_course_activation_is_transactional(void)
{
    fzx_use_demo();
    drive(0, 147);
    fzx_frame_t before = *fzx_render();
    fzx_course_t good = square_course();
    CHECK(fzx_validate_course(&good));
    fzx_course_t *upload = fzx_course_buffer();
    CHECK(upload != NULL);
    for (unsigned fault = 0; fault < 9; ++fault) {
        *upload = good;
        switch (fault) {
        case 0: upload->count = 3; break;
        case 1: upload->count = FZX_MAX_POINTS + 1; break;
        case 2: upload->points[0].radius_left = 0; break;
        case 3: upload->points[0].radius_right = FX(257); break;
        case 4: upload->points[0].tension = FX(2) + 1; break;
        case 5: upload->points[0].reserved = 1; break;
        case 6: upload->points[0].position.x = FX(4097); break;
        case 7: upload->points[1].position = upload->points[0].position; break;
        default:
            /* Structurally acceptable, but its zero start tangent cannot make
             * a drivable ring. Rejection must still preserve live state. */
            upload->points[0].tension = 0;
            CHECK(fzx_validate_course(upload));
            break;
        }
        CHECK(fzx_activate_course() == 0);
        same_frame(&before, fzx_render());
    }
    /* Opposing banks cancel the interpolated up vector halfway along this
     * square's horizontal segments. The former fixed-X fallback was parallel
     * to the tangent there and produced a zero camera basis / empty frame.
     * Reject the sampled geometry without disturbing the current race. */
    *upload = good;
    for (unsigned i = 0; i < upload->count; ++i)
        upload->points[i].bank = (sh2z_angle_t)((i & 1u) ? SH2Z_ANGLE_HALF : 0);
    CHECK(fzx_validate_course(upload));
    CHECK(fzx_activate_course() == 0);
    same_frame(&before, fzx_render());
    drive(147, 191);
    fzx_frame_t after = *fzx_render();
    fzx_use_demo();
    drive(0, 191);
    same_frame(&after, fzx_render()); /* Rejection preserves movement state too. */
    *upload = good;
    CHECK(fzx_activate_course() == 1);
    fzx_frame_t imported = *fzx_render();
    CHECK(imported.flags & FZX_FRAME_IMPORTED);
    CHECK(imported.speed == 0 && imported.lap == 1);
    upload->count = 0;
    memset(upload->points, 0, sizeof(upload->points));
    same_frame(&imported, fzx_render()); /* Active data must be copied, not aliased. */
    fzx_use_demo();
    CHECK(fzx_render()->flags == 0);
}

static void
test_laps_clipping_and_exact_transform_modes(void)
{
    fzx_course_t *upload = fzx_course_buffer();
    *upload = square_course();
    /* Four distinct banks exercise the complete camera/clip path while driving. */
    upload->points[0].bank = 0;
    upload->points[1].bank = 2048;
    upload->points[2].bank = 8192;
    upload->points[3].bank = 63536;
    CHECK(fzx_activate_course() == 1);
    uint32_t previous_lap = 1;
    unsigned compared_frames = 0;
    for (unsigned tick = 0; tick < 1200; ++tick) {
        /* Centered acceleration completes laps; alternating later steering
         * reaches both barriers and changes every view transform. */
        fzx_step(tick < 600 ? FZX_ACCEL : controls(tick));
        if (tick % 7 != 0 && tick != 1199) continue;
        fzx_set_transform_mode(0);
        fzx_frame_t general = *fzx_render();
        check_frame(&general);
        CHECK(general.lap >= previous_lap);
        previous_lap = general.lap;
        fzx_set_transform_mode(1);
        const fzx_frame_t *prepared = fzx_render();
        check_frame(prepared);
        same_frame(&general, prepared);
        ++compared_frames;
    }
    CHECK(previous_lap > 1);
    CHECK(compared_frames > 150);
    printf("Compared %u driven frames exactly; reached lap %u\n", compared_frames, previous_lap);
}

int
main(void)
{
    test_spline_against_independent_hermite_oracle();
    test_controls_reset_and_determinism();
    test_course_activation_is_transactional();
    test_laps_clipping_and_exact_transform_modes();
    printf("Core tests passed: %u checks (active with NDEBUG)\n", checks);
    return EXIT_SUCCESS;
}
