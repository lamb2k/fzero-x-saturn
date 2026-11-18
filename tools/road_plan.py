#!/usr/bin/env python3
"""Retain the pinned source course planner, replacing presentation writes only."""
from pathlib import Path
import argparse, hashlib, os, re

ROOT=Path(__file__).resolve().parents[1]
SOURCE_SHA='f97ca81943ab1a22feefcbba1b5200f87d39acdb2a0c954013421a0706bbffa0'

def stock(text):
    """Select only the two explicitly disabled fork guards in this function."""
    active=[True]; result=[]
    for line in text.splitlines(True):
        if line.startswith('#ifdef '):
            if line.strip() not in ('#ifdef PORT','#ifdef EXPANSION_KIT'):
                raise ValueError('Unreviewed source conditional')
            active.append(False)
        elif line.startswith('#else'):
            if len(active)==1:raise ValueError('Unbalanced source conditional')
            active[-1]=not active[-1]
        elif line.startswith('#endif'):
            if len(active)==1:raise ValueError('Unbalanced source conditional')
            active.pop()
        elif line.startswith('#'):
            raise ValueError('Unreviewed source directive')
        elif all(active):result.append(line)
    if len(active)!=1:raise ValueError('Unbalanced source conditional')
    return ''.join(result)

def replace_once(text,old,new):
    if text.count(old)!=1:raise ValueError('Source planner boundary changed: '+old[:60])
    return text.replace(old,new)

def native(source):
    if hashlib.sha256(source.encode()).hexdigest()!=SOURCE_SHA:
        raise ValueError('Source course changed; review planner extraction')
    marker='Gfx* Course_Draw(Gfx* gfx, s32 cameraIndex) {\n#endif\n'
    body=source.split(marker)[1].split('\n}\n',1)[0]
    body=stock(body)
    body=replace_once(body,'    sCourseDisp = gfx;','')
    body=body.replace('return sCourseDisp;','return;')
    body=replace_once(body,'    temp_fv1 = (camera->basis.x.x',
        '    out->outer = D_800F89D8;\n\n    temp_fv1 = (camera->basis.x.x')
    body=replace_once(body,'    if (D_800CF500 == 0) {',
        '    out->visible_chunks = D_800CF500;\n    if (D_800CF500 == 0) {')
    body=replace_once(body,'if (++segmentChunkGroup == sSegmentChunkGroups + ARRAY_COUNT(sSegmentChunkGroups)) {\n                return;',
        'if (++segmentChunkGroup == sSegmentChunkGroups + ARRAY_COUNT(sSegmentChunkGroups)) {\n                out->faults = 2; return;')
    body=replace_once(body,'    gSPDisplayList(sCourseDisp++, D_8014008);\n    gSPDisplayList(sCourseDisp++, D_8014078);','')
    body=replace_once(body,'gSPFogPosition(sCourseDisp++, sCourseFogStartDistance, 1000);',
        'out->fog_start = sCourseFogStartDistance;')
    body=replace_once(body,'gDPSetPrimColor(sCourseDisp++, 0, 0, 255 - ((gGameFrameCount * 8) % 256), 0, 0, 255);',
        'out->primitive[0] = 255 - ((gGameFrameCount * 8) % 256); out->primitive[3] = 255;')
    body=replace_once(body,'gDPSetFogColor(sCourseDisp++, var_a0, var_t0, var_a2, 255);',
        'out->fog[0] = var_a0; out->fog[1] = var_t0; out->fog[2] = var_a2; out->fog[3] = 255;')
    body=replace_once(body,'    sLastTrackShapeType = D_800F892C = -1;\n    D_800F8958[0].chunk = D_800F8958[1].chunk = NULL;\n    D_800F89C0 = 0;','')
    for name,direction in [('Forward',1),('Backward',-1)]:
        body=replace_once(body,'Course_Draw'+name+'ChunkGroup(furthestChunkGroup);',
            'ref_road_group(out, furthestChunkGroup, %d);'%direction)
    body=replace_once(body,'    if (D_800F892C == 1) {\n        gSPTexture(sCourseDisp++, 0, 0, 0, 1, G_OFF);\n    } else {\n        gSPTexture(sCourseDisp++, 0, 0, 0, 5, G_OFF);\n    }','')
    if re.search(r'\bg(?:SP|DP)\w+|\bsCourseDisp\b|\bgfx\b',body):
        raise ValueError('Native planner still contains display-list operations')
    body=body.replace('gGameFrameCount','draw_frame').replace('gCurrentCourseInfo->courseFogColors','course_fog')
    # FZX_ROAD_PLAN_FIXED (default 0): the per-chunk visibility loop in fixed point
    # (ref/road_plan_fixed.inc). Knob off compiles the source loop unchanged.
    start=body.index('    chunk = gSegmentChunks;\n    do {\n        chunk->depth =')
    end=body.index('    } while (chunk != sLastSegmentChunk);\n',start)+len('    } while (chunk != sLastSegmentChunk);\n')
    if body.count('    chunk = gSegmentChunks;\n    do {\n        chunk->depth =')!=1:
        raise ValueError('Source chunk visibility loop changed; review planner extraction')
    body=(body[:start]+'#if FZX_ROAD_PLAN_FIXED\n    ref_road_plan_cull_fixed(&sp60);\n#else\n'
          +body[start:end]+'#endif\n'+body[end:])
    prefix='''
#include "road_plan.h"
_Static_assert(SEGMENT_CHUNK_GROUP_COUNT == REF_ROAD_GROUPS, "source group bound");
static void ref_road_group(RefRoadPlan *out, const SegmentChunkGroup *group, int direction) {
    RefRoadGroup *dest;
    if(out->count >= REF_ROAD_GROUPS) {out->faults=3; return;}
    dest=&out->groups[out->count++];
    dest->start=group->startChunk-gSegmentChunks;
    dest->end=group->endChunk-gSegmentChunks;
    dest->direction=direction; dest->average_depth=group->averageDepth;
}
#if FZX_ROAD_PLAN_FIXED
#include "road_plan_fixed.inc"
#endif
void ref_road_plan(RefRoadPlan *out, unsigned int draw_frame, const unsigned char course_fog[3]) {
    const s32 cameraIndex=0;
    if(!out) return;
    memset(out,0,sizeof(*out));
    if(!course_fog || gSegmentChunkCount<3 || gSegmentChunkCount>1024 ||
       sLastSegmentChunk!=gSegmentChunks+gSegmentChunkCount) {out->faults=1;return;}
'''
    return prefix+body+'\n}\n'

def generate(source_path, output):
    source=source_path.read_text()
    added=native(source)
    # Oracle callbacks are enabled only by the HOST test macro. The production
    # compilation unit retains the original planner and discards it at link.
    marker='Gfx* Course_Draw(Gfx* gfx, s32 cameraIndex) {\n#endif\n'
    body=source.split(marker)[1].split('\n}\n',1)[0]
    oracle_body=body
    for direction in ('Forward','Backward'):
        old='Course_Draw'+direction+'ChunkGroup(furthestChunkGroup);'
        new=('#ifdef REF_ROAD_PLAN_ORACLE\n'
             'ref_road_oracle_'+direction.lower()+'(furthestChunkGroup);\n'
             '#else\n'+old+'\n#endif')
        oracle_body=replace_once(oracle_body,old,new)
    declarations='''
#ifdef REF_ROAD_PLAN_ORACLE
static void ref_road_oracle_forward(SegmentChunkGroup*);
static void ref_road_oracle_backward(SegmentChunkGroup*);
#endif
'''
    source=source.replace(marker,declarations+marker).replace(body,oracle_body)
    added+='\n#ifdef REF_ROAD_PLAN_ORACLE\n#include "road_plan_oracle.inc"\n#endif\n'
    # No upstream edits.
    # -ffunction-sections and the normal target linker discard unused graphics.
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_bytes((source+'\n'+added).encode())
    files=[Path(__file__),ROOT/'ref/road_plan.h',ROOT/'ref/road_plan_oracle.inc',ROOT/'ref/road_plan_fixed.inc',source_path]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    lines=['# fzx generation receipt v1; regenerate, never edit']
    lines+=['generator %s %s'%(sha(p),os.path.relpath(p,ROOT/'tools')) for p in files]
    lines+=['output %s %s'%(sha(output),output.name)]
    output.with_suffix('.receipt').write_bytes(('\n'.join(lines)+'\n').encode())

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',type=Path,default=ROOT/'upstream/G-Diffuser/decomp/src/game/course.c')
    ap.add_argument('--output',type=Path,default=ROOT/'generated/road_plan/course.c')
    args=ap.parse_args();generate(args.source,args.output)
