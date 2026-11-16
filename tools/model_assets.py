"""Native fixed-pose machine probe pack; source geometry stays local.

The poses are diagnostic views, not a replacement race camera. Projection and
source-HLE lighting are prepared offline so this first target probe measures
the bounded material/raster path independently of dynamic pose integration.
"""
import math
import re


def sampler(binding, texture):
    if binding["tile"] == 0:
        return {k: binding["sampling"][k] for k in ("s_mode", "t_mode", "s_mask", "t_mask", "s_shift", "t_shift")}
    args = binding["arguments"]
    four_bit = binding["source_command"].endswith("_4b")
    width_index = 3 if four_bit else 4
    width, height = map(int, args[width_index:width_index+2])
    if (width, height) != (texture["width"], texture["height"]):
        raise ValueError("Model texture binding dimensions disagree")
    expected = {"RGBA16": "G_IM_FMT_RGBA", "I4": "G_IM_FMT_I", "IA4": "G_IM_FMT_IA"}[texture["format"]]
    if args[2] != expected or (not four_bit and args[3] != "G_IM_SIZ_16b"):
        raise ValueError("Model texture binding format disagrees")
    values = args[width_index+3:]
    if len(values) != 6:
        raise ValueError("Unsupported model sampling state")
    def mode(value):
        bits = {"G_TX_NOMIRROR": 0, "G_TX_WRAP": 0, "G_TX_MIRROR": 1, "G_TX_CLAMP": 2}
        result = 0
        for token in value.split("|"):
            result |= bits[token.strip()]
        return result
    def number(value):
        return 0 if value == "G_TX_NOLOD" else int(value, 0)
    return dict(zip(("s_mode", "t_mode", "s_mask", "t_mask", "s_shift", "t_shift"),
                    [mode(values[0]), mode(values[1]), *map(number, values[2:])]))


def source_probe_state(racer_source, background_source):
    match = re.search(r"u8 D_800CE3D0\[\] = \{(.*?)\};", racer_source, re.S)
    if not match:
        raise ValueError("Missing source race reflection table")
    lut = [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", match[1])]
    if len(lut) != 256 or any(not 0 <= v <= 255 for v in lut):
        raise ValueError("Invalid source race reflection table")
    match = re.search(r"\{ CUSTOM_MACHINE_DEFAULT, 0, 0, \{([^}]+)\}, \{([^}]+)\}, \{([^}]+)\}.*?// CAPTAIN_FALCON", racer_source)
    if not match:
        raise ValueError("Missing source Blue Falcon colors")
    body = [int(match[i].split(",")[0].strip()) for i in (1, 2, 3)]
    match = re.search(r"CourseSkyboxes sSkyboxPurple = \{([^}]+)\}", background_source)
    if not match:
        raise ValueError("Missing source purple-sky lighting")
    # CourseSkyboxes: texture, sky RGB, cloud RGB, racer-fog RGB, flags.
    fog = list(map(int, [x.strip() for x in match[1].split(",")][7:10]))
    return lut, body, fog


def unit(vector):
    length = math.sqrt(sum(v*v for v in vector))
    if not length:
        raise ValueError("Degenerate probe light")
    return [v/length for v in vector]


def rotate(vector, yaw, pitch):
    sy, cy, sx, cx = math.sin(yaw), math.cos(yaw), math.sin(pitch), math.cos(pitch)
    x, y, z = vector
    return [cy*x+sy*z, cx*y-sx*(-sy*x+cy*z), sx*y+cx*(-sy*x+cy*z)]


def project(model, yaw_degrees, pitch_degrees, distance, lut):
    yaw, pitch = map(math.radians, (yaw_degrees, pitch_degrees))
    light = unit([69, 69, 69])
    # Source Racer_Draw packs round((worldLight-cameraForward)*16383) as s16.
    half = unit([int((value - (i == 2))*16383 + (0.5 if value-(i == 2) >= 0 else -0.5))
                 for i, value in enumerate([0.57735]*3)])
    vertices = []
    for v in model["vertices"]:
        point = rotate([v["position"][0], v["position"][1]-75, v["position"][2]], yaw, pitch)
        point[2] += distance
        if point[2] < 513:
            raise ValueError("Model probe requires all vertices in front of near bound")
        x, y = round((64+160*point[0]/point[2])*16), round((64-160*point[1]/point[2])*16)
        inverse_z = round(2**24 / point[2])
        normal = rotate(v["normal"], yaw, pitch)
        diffuse = max(0, sum(a*b for a, b in zip(normal, light))/127)
        shade = min(255, int(100+255*diffuse))
        index = max(-128, min(127, int(sum(a*b for a, b in zip(normal, half)))))
        alpha = lut[128+index]
        vertices.append([x, y, inverse_z, *v["uv"], shade*257, shade*257, shade*257, alpha*257])
    return vertices


def header(models, bindings, textures, raw, lut, body, fog, *, fixture=False):
    """All texture bytes and every triangle stay source-derived for retail.

    Quantization is explicit: XY 1/16 pixel; reciprocal depth 2^24/z; source
    UVs untouched; shade UNORM16 from the HLE's 8-bit vertex outputs.
    """
    by_name = {t["symbol"]: t for t in textures}
    lines = ['/* Generated fixed-pose native machine probe. Never edit. */', '#include "fzx_model.h"',
             f"#define FZX_MODEL_FIXTURE {int(fixture)}", "#define FZX_MODEL_VIEW_COUNT 8u",
             "static const uint8_t fzx_model_body[3]={"+",".join(map(str, body))+"};",
             "static const uint8_t fzx_model_fog[3]={"+",".join(map(str, fog))+"};"]
    for binding in bindings:
        tile, symbol = binding["tile"], binding["symbol"]
        data = raw[symbol]
        lines += [f"static const uint8_t fzx_model_tex_{tile}[]={{"]
        lines.extend("    "+",".join(str(v) for v in data[i:i+24])+"," for i in range(0, len(data), 24))
        lines += ["};"]
    lines += ["static const fzx_material_texture_t fzx_model_textures[]={"]
    for binding in bindings:
        tile, symbol = binding["tile"], binding["symbol"]
        t = by_name[symbol]
        state = sampler(binding, t)
        fmt = {"RGBA16": 0, "I4": 1, "IA4": 2}[t["format"]]
        values = [len(raw[symbol]), t["width"], t["height"], fmt, *state.values()]
        lines += [f"    {{fzx_model_tex_{tile},"+",".join(map(str, values))+"},"]
    lines += ["};"]
    for i, model in enumerate(models):
        lines += [f"static const fzx_model_triangle_t fzx_model_tri_{i}[]={{"]
        for tri in model["triangles"]:
            lines += ["    {{"+",".join(map(str, tri["vertices"]))+"},"+str(tri["material"])+"},"]
        lines += ["};", f"static const fzx_model_material_t fzx_model_mat_{i}[]={{"]
        for m in model["materials"]:
            env = m["environment"]
            is_body = env == "racer.bodyRGB"
            if is_body:
                env = [0, 0, 0]
            primitive = m["primitive"] or [0, 0, 0]
            alpha = m["combine"][0] == "TEXEL0"
            lines += ["    {{{"+",".join(map(str, primitive[:3]))+"},{"+",".join(map(str, env[:3]))+"},"+
                      str(int(alpha))+"},"+str(m["tile"])+","+str(int(is_body))+"},"]
        lines += ["};"]
    poses = [(0, 0, -25, 1200), (0, 40, -25, 1200), (0, -40, -25, 1200), (0, 180, -25, 1200),
             (1, 20, -25, 1200), (2, 20, -25, 1200), (3, 20, -25, 1200), (4, 20, -25, 1200)]
    for i, (lod, yaw, pitch, distance) in enumerate(poses):
        vertices = project(models[lod], yaw, pitch, distance, lut)
        lines += [f"static const fzx_model_vertex_t fzx_model_vertices_{i}[]={{"]
        for v in vertices:
            lines += ["    {"+",".join(map(str, v[:5]))+",{"+",".join(map(str, v[5:]))+"}},"]
        lines += ["};"]
    lines += ["static const fzx_model_view_t fzx_model_views[]={"]
    for i, (lod, *_rest) in enumerate(poses):
        m = models[lod]
        lines += [f"    {{fzx_model_vertices_{i},fzx_model_tri_{lod},fzx_model_mat_{lod},"+
                  f"{len(m['vertices'])},{len(m['triangles'])},{len(m['materials'])}}},"]
    lines += ["};", ""]
    return "\n".join(lines).encode()
