"""HOST contact-incident stream for tools/rivals_contact_judge.py (C-108): line_field_contact_diag.py SRC OUT.
Writes a patched copy of SH2ZAM's saturn/src/sh2zam_line_field.c for the HOST runner only (never the engine).
Every touching pair in fld_contacts writes one line to $FLD_DIAG (all passes; dedupe by tick offline):
tick ia ib a_ext b_ext fresh tube half_width gap adl dl closing dlatvel norm dmg_a dmg_b a_attacks b_attacks.
The values are the ones the step itself uses (gap, wrapped dl, closing before the bump), so the race is unchanged."""
import sys
s = open(sys.argv[1]).read()


def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)


rep('#include "sh2zam/line_field_experimental.h"\n',
    '#include "sh2zam/line_field_experimental.h"\n#include <stdio.h>\n#include <stdlib.h>\n'
    'static FILE *dg_f; static int dg_init;\n'
    'static FILE *dg_out(void) { if(!dg_init) { const char *p = getenv("FLD_DIAG"); dg_f = p ? fopen(p, "w") : NULL; dg_init = 1; } return dg_f; }\n')
# remember whether the seam sample was taken (tube) and its half width
rep('            if(fld_tube(p, line->nodes[a->t.node].flags)) {\n                sh2z_line_sample(line, a->t.node, a->t.dist, &at);\n',
    '            int dg_tube = 0; int32_t dg_hw = 0;\n'
    '            if(fld_tube(p, line->nodes[a->t.node].flags)) {\n                sh2z_line_sample(line, a->t.node, a->t.dist, &at);\n'
    '                dg_tube = 1; dg_hw = at.half_width;\n')
rep('            if(fresh && bh && bh->contact_norm > 0) {\n',
    '            {\n'
    '                const int32_t ax = fld_abs(gap), ay = adl, len = (ax > ay ? ax + (ay >> 1) : ay + (ax >> 1));\n'
    '                const int64_t dot = (int64_t)closing * gap + (int64_t)(b->t.lat_vel - a->t.lat_vel) * dl;\n'
    '                const int32_t norm = len > 0 ? (int32_t)((dot < 0 ? -dot : dot) / len) : fld_abs(closing);\n'
    '                const int32_t share = bh && bh->contact_norm > 0 ? fld_mul_q16(norm, bh->contact_norm) : 0;\n'
    '                const int32_t da = fresh ? (a_attacks ? 0 : (b_attacks ? share * 2 : share)) : 0;\n'
    '                const int32_t db = fresh ? (b_attacks ? 0 : (a_attacks ? share * 2 : share)) : 0;\n'
    '                FILE *o = dg_out();\n'
    '                if(o) fprintf(o, "%u %u %u %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\\n", (unsigned)f->tick, (unsigned)ia, (unsigned)ib,\n'
    '                              a_ext, b_ext, fresh, dg_tube, (int)dg_hw, (int)gap, (int)adl, (int)dl, (int)closing,\n'
    '                              (int)(b->t.lat_vel - a->t.lat_vel), (int)norm, (int)da, (int)db, a_attacks, b_attacks);\n'
    '            }\n'
    '            if(fresh && bh && bh->contact_norm > 0) {\n')
open(sys.argv[2], 'w').write(s)
