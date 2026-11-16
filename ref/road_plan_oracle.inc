/* HOST only: the unchanged source planner emits real Gfx state commands.
 * Its two geometry callbacks only observe group boundaries. The native path
 * is then compared without sharing any native extraction substitutions. */
static RefRoadPlan road_oracle;
static void ref_road_oracle_group(SegmentChunkGroup *group,int direction) {
    RefRoadGroup *dest;
    if(road_oracle.count>=REF_ROAD_GROUPS) {road_oracle.faults=1;return;}
    dest=&road_oracle.groups[road_oracle.count++];
    dest->start=group->startChunk-gSegmentChunks;
    dest->end=group->endChunk-gSegmentChunks;
    dest->direction=direction;dest->average_depth=group->averageDepth;
}
static void ref_road_oracle_forward(SegmentChunkGroup *group) {ref_road_oracle_group(group,1);}
static void ref_road_oracle_backward(SegmentChunkGroup *group) {ref_road_oracle_group(group,-1);}
int ref_road_plan_check(RefRoadPlan *out,unsigned int draw_frame,const unsigned char fog[3]) {
    Gfx cmds[32],*end;
    unsigned int i,seen=0;
    RefRoadPlan repeated;
    unsigned int saved_frame=gGameFrameCount;
    unsigned char saved_fog[3];
    for(i=0;i<3;++i) {saved_fog[i]=gCurrentCourseInfo->courseFogColors[i];
                      gCurrentCourseInfo->courseFogColors[i]=fog[i];}
    gGameFrameCount=draw_frame;
    memset(&road_oracle,0,sizeof(road_oracle));
    memset(cmds,0xA5,sizeof(cmds));
    end=Course_Draw(cmds,0);
    gGameFrameCount=saved_frame;
    for(i=0;i<3;++i)gCurrentCourseInfo->courseFogColors[i]=saved_fog[i];
    if(end<cmds || end>=cmds+31 || cmds[31].words.w0!=0xA5A5A5A5u ||
       cmds[31].words.w1!=0xA5A5A5A5u) return 1;
    road_oracle.visible_chunks=D_800CF500;
    road_oracle.outer=D_800F89D8;
    ref_road_plan(out,draw_frame,fog);
    if(out->faults || road_oracle.faults) return 2;
    if(out->count!=road_oracle.count || out->visible_chunks!=road_oracle.visible_chunks ||
       out->outer!=road_oracle.outer) return 3;
    if(memcmp(out->groups,road_oracle.groups,sizeof(out->groups))) return 4;
    for(i=0;i<(unsigned int)(end-cmds);++i) {
        unsigned int a=cmds[i].words.w0,b=cmds[i].words.w1,wanted;
        if((a>>24)==0xFA || (a>>24)==0xF8) {
            const unsigned char *rgba=(a>>24)==0xFA?out->primitive:out->fog;
            wanted=(unsigned int)rgba[0]<<24|(unsigned int)rgba[1]<<16|
                   (unsigned int)rgba[2]<<8|rgba[3];
            if(b!=wanted) return 5;
            seen|=(a>>24)==0xFA?1u:2u;
        }
        if(a==0xDB080000u) {
            const int denominator=1000-out->fog_start;
            if(denominator<=0) return 6;
            wanted=((128000/denominator)&65535u)<<16|
                   (((500-out->fog_start)*256/denominator)&65535u);
            if(b!=wanted) return 7;
            seen|=4u;
        }
    }
    if(out->count && seen!=7u) return 8;
    ref_road_plan(&repeated,draw_frame,fog);
    if(memcmp(out,&repeated,sizeof(*out))) return 9;
    return 0;
}
/* Synthetic state injection is confined to this HOST oracle. It exercises
 * interior and both join transitions even when a fuzz driver crashes early. */
int ref_road_plan_branch_check(void) {
    Racer *r=&gRacers[gCameras[0].id];
    Racer saved=*r;
    CourseSegment segment=*r->segmentPositionInfo.courseSegment;
    RefRoadPlan out;
    const unsigned char fog[3]={203,137,61};
    unsigned int flags,i;int result=0;
    segment.previousJoinEndTValue=0.2f;segment.nextJoinStartTValue=0.8f;segment.joinScale=0.25f;
    r->segmentPositionInfo.courseSegment=&segment;
    for(flags=0;flags<8 && !result;++flags) {
        segment.trackSegmentInfo=(flags&1?TRACK_FLAG_INSIDE:0)|
             (flags&2?TRACK_JOIN_PREVIOUS:0)|(flags&4?TRACK_JOIN_NEXT:0);
        for(i=0;i<3 && !result;++i) {
            r->segmentPositionInfo.segmentTValue=i==0?0.1f:i==1?0.5f:0.9f;
            r->segmentPositionInfo.segmentLengthProportion=i==0?0.125f:i==1?0.5f:0.875f;
            result=ref_road_plan_check(&out,flags*3+i,fog);
        }
    }
    *r=saved;
    return result;
}
