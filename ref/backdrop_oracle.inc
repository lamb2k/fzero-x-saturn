/* HOST-only comparison with original Background_Update and its GfxPool writes.
 * It shares source camera inputs, but none of the native storage substitutions. */
static GfxPool backdrop_oracle_pools[2];
static int backdrop_oracle_ready;

int ref_backdrop_check_init(float sx,float sz) {
    backdrop_oracle_ready=0;
    if(ref_backdrop_init(sx,sz))return 1;
    memset(sBackgrounds,0,sizeof(sBackgrounds));
    memset(backdrop_oracle_pools,0,sizeof(backdrop_oracle_pools));
    sBackgroundCount=1;sCloudCount=1;sSkyboxFlags=SKYBOX_CLOUDY;
    sBackgroundSpriteCount=0;
    Background_InitBackgroundInfo(&gCameras[0],sBackgrounds,&sVenueFloorMuteCity,sx,sz);
    backdrop_oracle_ready=1;return 0;
}

int ref_backdrop_check(RefBackdrop* out,unsigned int pool,int paused) {
    GfxPool* saved_pool=gGfxPool;
    s8 saved_paused=gGamePaused;
    Camera camera=gCameras[0];
    RefBackdrop expected;
    unsigned int i,j;
    if(!out || !backdrop_oracle_ready || pool>1)return 1;
    gGfxPool=&backdrop_oracle_pools[pool];gGamePaused=paused!=0;
    Background_Update();
    memset(&expected,0,sizeof(expected));expected.count=20;
    memcpy(expected.state,sBackgrounds,sizeof(expected.state));
    for(i=0;i<20;++i) {
        const Vtx* v=&gGfxPool->unk_29B48[i];
        for(j=0;j<3;++j)expected.vertices[i].position[j]=v->v.ob[j];
        expected.vertices[i].flag=v->v.flag;
        for(j=0;j<2;++j)expected.vertices[i].uv[j]=v->v.tc[j];
        for(j=0;j<4;++j)expected.vertices[i].color[j]=v->v.cn[j];
    }
    gGfxPool=saved_pool;gGamePaused=saved_paused;
    ref_backdrop_tick(out,pool,paused);
    if(memcmp(&camera,&gCameras[0],sizeof(camera)))return 2;
    if(out->faults || out->count!=20)return 3;
    if(memcmp(out->state,expected.state,sizeof(expected.state)))return 4;
    if(memcmp(out->vertices,expected.vertices,sizeof(expected.vertices)))return 5;
    return 0;
}

int ref_backdrop_branch_check(void) {
    Camera saved=gCameras[0];
    RefBackdrop out,paused_before;
    unsigned int i,sign;
    const float heights[]={900.0f,949.0f,950.0f,951.0f,1000.0f,-700.0f};
    /* Source initialization draws one magnitude and two signs. Test every
     * possible result, including 10 * 0.0005f (one ulp above literal 0.005f). */
    for(i=0;i<=10;++i)for(sign=0;sign<4;++sign) {
        const float speed=i*0.0005f;
        if(ref_backdrop_check_init(sign&1 ? -speed : speed,sign&2 ? -speed : speed) ||
           ref_backdrop_check(&out,0,0)) {gCameras[0]=saved;return 7;}
    }
    if(ref_backdrop_check_init(0.005f,-0.004f))return 1;
    for(i=0;i<sizeof(heights)/sizeof(heights[0]);++i) {
        gCameras[0].eye.y=heights[i];
        if(ref_backdrop_check(&out,i&1,0)) {gCameras[0]=saved;return 2;}
    }
    paused_before=out;
    if(ref_backdrop_check(&out,0,1) ||
       memcmp(paused_before.state,out.state,sizeof(out.state))) {
        gCameras[0]=saved;return 3;
    }
    /* Both source quads advance scroll; the wrapping branch is exercised at
     * each sign, while pool choices deliberately repeat instead of toggling. */
    sBackgrounds[0].cloudScroll.xScroll=native_backgrounds[0].cloudScroll.xScroll=1023.999f;
    sBackgrounds[0].cloudScroll.zScroll=native_backgrounds[0].cloudScroll.zScroll=-1023.999f;
    if(ref_backdrop_check(&out,0,0)) {gCameras[0]=saved;return 4;}
    ref_backdrop_tick(&out,2,0);
    if(!out.faults) {gCameras[0]=saved;return 5;}
    if(!ref_backdrop_init(0.006f,0)) {gCameras[0]=saved;return 6;}
    gCameras[0]=saved;
    return ref_backdrop_check_init(0.0035f,-0.002f);
}

/* FZX_BACKDROP_CATCHUP against the original: SPAN source Background_Update calls
 * (oracle storage) against the native catch-up (SPAN-1 scroll steps plus one full
 * update, itself checked against SPAN full native updates from the same state). */
int ref_backdrop_catchup_oracle(RefBackdrop* out,unsigned int span,unsigned int pool,int paused) {
    GfxPool* saved_pool=gGfxPool;
    s8 saved_paused=gGamePaused;
    Camera camera=gCameras[0];
    RefBackdrop expected;
    unsigned int i,j;
    int r;
    if(!out || !span || !backdrop_oracle_ready || pool>1)return 1;
    gGfxPool=&backdrop_oracle_pools[pool];gGamePaused=paused!=0;
    for(i=0;i<span;++i)Background_Update();
    memset(&expected,0,sizeof(expected));expected.count=20;
    memcpy(expected.state,sBackgrounds,sizeof(expected.state));
    for(i=0;i<20;++i) {
        const Vtx* v=&gGfxPool->unk_29B48[i];
        for(j=0;j<3;++j)expected.vertices[i].position[j]=v->v.ob[j];
        expected.vertices[i].flag=v->v.flag;
        for(j=0;j<2;++j)expected.vertices[i].uv[j]=v->v.tc[j];
        for(j=0;j<4;++j)expected.vertices[i].color[j]=v->v.cn[j];
    }
    gGfxPool=saved_pool;gGamePaused=saved_paused;
    r=ref_backdrop_catchup_check(out,span,pool,paused);
    if(r)return 10+r;
    if(memcmp(&camera,&gCameras[0],sizeof(camera)))return 2;
    if(out->faults || out->count!=20)return 3;
    if(memcmp(out->state,expected.state,sizeof(expected.state)))return 4;
    if(memcmp(out->vertices,expected.vertices,sizeof(expected.vertices)))return 5;
    return 0;
}
/* Both sides' scroll near the wrap points, so catch-up spans cross them. */
void ref_backdrop_catchup_wrap_preset(void) {
    sBackgrounds[0].cloudScroll.xScroll=native_backgrounds[0].cloudScroll.xScroll=1023.99f;
    sBackgrounds[0].cloudScroll.zScroll=native_backgrounds[0].cloudScroll.zScroll=-1023.99f;
}
