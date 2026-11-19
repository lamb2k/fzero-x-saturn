/* HOST check of an engine 3D vehicle pack (tools/mach3d_packs.py MACH3Dnn.PAK).
 *   mach3d-check PACK METADATA_BYTES TEXEL_BYTES
 * 1. Opens the metadata half with fzx_vehicle_pack_open (as the engine) and refuses three
 *    corrupted copies: one byte short, one metadata byte flipped, one texel byte flipped
 *    (the CD loader's FNV of the whole pack).
 * 2. Draws LOD 1 through the native arm (FZX_VEHICLE_NATIVE, the same culling) at the E1
 *    chase view (the car 115.6 units ahead, seen 34.5 degrees from above and behind) and
 *    over a yaw sweep at three view pitches (rivals seen from other cars), printing the
 *    VDP1 commands per view: chase, and the maximum.
 * Prints one JSON line. Exit 0 only if every corrupted copy was refused. MIT. */
#include "fzx_vehicle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fzx_vehicle_work_t work;
static unsigned emits;
static bool emit(void *ctx, const fzx_vehicle_quad_t *q) { (void)ctx; (void)q; ++emits; return true; }
static uint32_t fnv(const uint8_t *p, unsigned n) { uint32_t h = 2166136261u; for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u; return h; }

static unsigned draw(const fzx_vehicle_pack_t *pack, double pitch_deg, double yaw_deg) {
    const double pi = 3.14159265358979323846, p = pitch_deg * pi / 180.0, y = yaw_deg * pi / 180.0;
    /* View space: the camera looks along -z. The car's frame before yaw: forward away
     * from the camera and pitched down by p, up tilted back, left = up x forward. */
    double f[3] = { 0, -sin(p), -cos(p) }, u[3] = { 0, cos(p), -sin(p) }, l[3];
    /* Yaw about the car's up axis. */
    double f2[3], cu[3];
    cu[0] = u[1] * f[2] - u[2] * f[1]; cu[1] = u[2] * f[0] - u[0] * f[2]; cu[2] = u[0] * f[1] - u[1] * f[0];
    for (int i = 0; i < 3; ++i) f2[i] = f[i] * cos(y) + cu[i] * sin(y);
    l[0] = u[1] * f2[2] - u[2] * f2[1]; l[1] = u[2] * f2[0] - u[0] * f2[2]; l[2] = u[0] * f2[1] - u[1] * f2[0];
    RefCamera camera = {0}; RefMachine state = {0};
    state.model_pos[2] = -115.6f;
    for (int i = 0; i < 3; ++i) {
        state.model_basis[0][i] = (float)f2[i]; state.model_basis[1][i] = (float)u[i]; state.model_basis[2][i] = (float)l[i];
        state.model_scale[i] = 0.1f; state.ambient[i] = 100; state.light_rgb[i] = 255; state.light_dir[i] = 69;
        state.body[i] = 128; state.fog[i] = pack->data[51 + i];
    }
    state.reflection[2] = 127; state.source_lod = 1; state.normal_body_visible = 1;
    const fzx_view_t view = {.rotation = {{1073741824, 0, 0}, {0, 1073741824, 0}, {0, 0, -1073741824}},
        .focal = {200 * 65536, 200 * 65536}, .center = {160 * 65536, 112 * 65536}, .near_plane = 16 * 4096,
        .far_plane = 2000 * 4096, .clip = {0, 0, 320, 224}};
    fzx_vehicle_stats_t stats;
    emits = 0;
    if (!fzx_vehicle_draw(pack, &view, &camera, &state, &work, 256, emit, 0, &stats)) return 9999;
    return stats.draws;
}

int main(int argc, char **argv) {
    if (argc != 4) return 1;
    const unsigned meta = (unsigned)atoi(argv[2]), texels = (unsigned)atoi(argv[3]);
    static uint8_t data[1 << 17];
    FILE *fp = fopen(argv[1], "rb"); if (!fp) return 2;
    const unsigned size = (unsigned)fread(data, 1, sizeof(data), fp); fclose(fp);
    if (size != meta + texels) return 2;
    const uint32_t whole = fnv(data, size), expected = fnv(data, meta);
    fzx_vehicle_pack_t pack, bad;
    const bool open_ok = fzx_vehicle_pack_open(&pack, data, meta, texels, expected, false);
    /* Corruption controls. */
    const bool short_refused = !fzx_vehicle_pack_open(&bad, data, meta - 1, texels, expected, false);
    data[meta / 2] ^= 0x10;
    const bool meta_refused = !fzx_vehicle_pack_open(&bad, data, meta, texels, expected, false);
    data[meta / 2] ^= 0x10;
    data[meta + texels / 2] ^= 0x10;
    const bool texel_refused = fnv(data, size) != whole; /* the CD loader checks the whole pack's FNV */
    data[meta + texels / 2] ^= 0x10;
    unsigned chase = 0, worst = 0, views = 0;
    double worst_yaw = 0, worst_pitch = 0;
    if (open_ok) {
        memset(&work, 0, sizeof(work));
        chase = draw(&pack, 34.46, 0.0);
        const double pitches[3] = { 10.0, 20.0, 34.46 };
        for (int k = 0; k < 3; ++k)
            for (int yaw = 0; yaw < 360; yaw += 15) {
                const unsigned n = draw(&pack, pitches[k], yaw); ++views;
                if (n > worst) { worst = n; worst_yaw = yaw; worst_pitch = pitches[k]; }
            }
    }
    printf("{\"open\": %s, \"faces_lod1\": %u, \"positions_lod1\": %u, \"normals_lod1\": %u, \"palettes\": %u, "
           "\"chase_commands\": %u, \"max_commands\": %u, \"max_at\": [%.1f, %.0f], \"views\": %u, "
           "\"refused\": {\"short\": %s, \"metadata_flip\": %s, \"texel_flip\": %s}}\n",
           open_ok ? "true" : "false", pack.lod[0].faces, pack.lod[0].positions, pack.lod[0].normals, pack.palettes,
           chase, worst, worst_pitch, worst_yaw, views,
           short_refused ? "true" : "false", meta_refused ? "true" : "false", texel_refused ? "true" : "false");
    return open_ok && short_refused && meta_refused && texel_refused ? 0 : 3;
}
