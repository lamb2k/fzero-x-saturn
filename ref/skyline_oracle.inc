/* HOST only: run original skyline initialization/update with its own storage. */
GfxPool D_8024DCE0[2];
static int skyline_oracle_ready;

int ref_skyline_check_init(void) {
    const s32 saved_course=gCourseIndex,saved_venue=gVenueType;
    skyline_oracle_ready=0;
    if(ref_skyline_init())return 1;
    memset(D_8024DCE0,0,sizeof(D_8024DCE0));
    memset(sBackgroundSprites,0,sizeof(sBackgroundSprites));
    gCourseIndex=0;gVenueType=0;sSkyboxFlags=SKYBOX_CLOUDY;
    Background_InitBackgroundSprites();
    gCourseIndex=saved_course;gVenueType=saved_venue;
    if(sBackgroundSpriteCount!=REF_SKYLINE_COUNT)return 2;
    skyline_oracle_ready=1;return 0;
}

int ref_skyline_check(RefSkyline* out,unsigned int pool) {
    GfxPool* saved=gGfxPool;
    unsigned int i,j;
    if(!out || !skyline_oracle_ready || pool>1)return 1;
    gGfxPool=&D_8024DCE0[pool];
    Background_UpdateBackgroundSprites();
    gGfxPool=saved;
    ref_skyline_tick(out,pool);
    if(out->faults || out->count!=sBackgroundSpriteCount)return 2;
    if(out->tint[0]!=sSkyboxPurple.backgroundSpriteR || out->tint[1]!=sSkyboxPurple.backgroundSpriteG ||
       out->tint[2]!=sSkyboxPurple.backgroundSpriteB || out->tint[3]!=255)return 3;
    for(i=0;i<4;++i) {
        const Vtx* v=&D_8024DCE0[pool].unk_2C4E8[i];
        for(j=0;j<3;++j)if(out->vertices[i].position[j]!=v->v.ob[j])return 4;
        for(j=0;j<2;++j)if(out->vertices[i].uv[j]!=v->v.tc[j])return 5;
        for(j=0;j<4;++j)if(out->vertices[i].color[j]!=v->v.cn[j])return 6;
        if(out->vertices[i].flag!=v->v.flag)return 7;
    }
    for(i=0;i<REF_SKYLINE_COUNT;++i) {
        const BackgroundSprite* sprite=&sBackgroundSprites[i];
        const Mtx* matrix=&D_8024DCE0[pool].unk_2C368[i];
        const RefSkylineInstance* instance=&out->instances[i];
        if(instance->sprite_id!=sprite->spriteId || instance->visible!=sprite->isVisible ||
           memcmp(instance->position,&sprite->pos,sizeof(instance->position)))return 8;
        for(j=0;j<16;++j) {
            int expected=(int)(short)matrix->u.i[j/4][j%4]*65536+(unsigned short)matrix->u.f[j/4][j%4];
            if(instance->matrix[j]!=expected)return 9;
        }
    }
    return 0;
}

int ref_skyline_branch_check(void) {
    const Camera saved=gCameras[0];
    RefSkyline out;
    unsigned int i;
    const float boundaries[]={-23001.0f,-23000.0f,-22999.0f,22999.0f,23000.0f,23001.0f};
    if(ref_skyline_check_init())return 1;
    for(i=0;i<6;++i) {
        gCameras[0].eye.x=boundaries[i]-sBackgroundSprites[0].pos.x;
        gCameras[0].eye.z=0.0f-sBackgroundSprites[0].pos.z;
        if(ref_skyline_check(&out,(i/2)&1) || out.instances[0].visible!=(i>0 && i<5)) {
            gCameras[0]=saved;return 2;
        }
        gCameras[0].eye.x=0.0f-sBackgroundSprites[0].pos.x;
        gCameras[0].eye.z=boundaries[i]-sBackgroundSprites[0].pos.z;
        if(ref_skyline_check(&out,(i/2)&1) || out.instances[0].visible!=(i>0 && i<5)) {
            gCameras[0]=saved;return 3;
        }
    }
    /* A vertical facing vector exercises the source's degenerate-basis fallback. */
    gCameras[0].eye.x=gCameras[0].eye.z=0;
    gCameras[0].basis.x.x=gCameras[0].basis.x.z=0;
    gCameras[0].basis.x.y=1;
    if(ref_skyline_check(&out,0)) {gCameras[0]=saved;return 4;}
    ref_skyline_tick(&out,2);
    if(!out.faults) {gCameras[0]=saved;return 5;}
    gCameras[0]=saved;
    return ref_skyline_check_init();
}
