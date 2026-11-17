"""Extract the bounded original machine/settings 2D draw and object closure.

Source owns placements, text metrics, gradient, pulse and object motion. Native
presentation consumes packets; model commands are an explicit ordered marker.
"""
import re
from import_source_art import DECOMP, ArtError
from menu_source import cartridge, function, COMMON, OBJECT
from course_menu_source import declaration

FONT = DECOMP/'src/overlays/ovl_i2/font.c'
RACER = DECOMP/'src/game/racer.c'
TEXTURE = DECOMP/'src/game/texture_utils.c'


def dependencies():
    return [FONT, RACER, TEXTURE, DECOMP/'include/fzx_font.h', DECOMP/'include/fzx_racer.h']


ADAPTER = r'''
typedef struct { uint16_t image; uint8_t kind,font; int32_t width,height,format; } unk_80077D50;
static void source_color(int32_t r,int32_t g,int32_t b,int32_t a) {
    m->color[0]=(uint8_t)r;m->color[1]=(uint8_t)g;m->color[2]=(uint8_t)b;m->color[3]=(uint8_t)a;
}
#define gDPSetPrimColor(g,x,y,r,b,c,a) source_color(r,b,c,a)
static fzx_machine_menu_draw_t *source_packet(uint8_t kind) {
    if(m->snapshot.count>=FZX_MACHINE_MENU_DRAW_MAX){m->fault=1;return NULL;}
    fzx_machine_menu_draw_t *d=&m->snapshot.draws[m->snapshot.count++];
    memset(d,0,sizeof(*d));d->kind=kind;return d;
}
static Gfx *source_emit(Gfx *gfx,const unk_80077D50 *tex,int32_t x,int32_t y,unsigned mode,
                        int32_t flip_x,int32_t flip_y,float scale_x,float scale_y) {
    if(!tex || x<INT16_MIN || x>INT16_MAX || y<INT16_MIN || y>INT16_MAX || mode>5 || flip_y ||
       !(scale_x>=0 && scale_x<=1) || !(scale_y>=0 && scale_y<=1)){m->fault=1;return gfx;}
    fzx_machine_menu_draw_t *d=source_packet(tex->kind);if(!d)return gfx;
    d->image=tex->image;d->font=tex->font;d->x=(int16_t)x;d->y=(int16_t)y;
    d->scale_x_q8=(uint16_t)(scale_x*256.0f+0.5f);d->scale_y_q8=(uint16_t)(scale_y*256.0f+0.5f);
    d->flip_x=(uint8_t)(mode==5 || (mode==2 && flip_x));
    if((mode==0 || mode==2 || mode==3 || mode==5) && tex->format!=4){d->r=d->g=d->b=d->a=255;}
    else {d->r=m->color[0];d->g=m->color[1];d->b=m->color[2];d->a=m->color[3];}
    return gfx;
}
#define func_80078EA0_impl(g,t,x,y,mode,a,b,c,d,e) source_emit(g,t,x,y,mode,a,b,c,d)
#define func_80077D50_impl(t,x,y) ((void)(t))
#define D_800E3F28 m->cache_images
#define PACK_5551(r,g,b,a) (((uint32_t)(r)<<11)|((uint32_t)(g)<<6)|((uint32_t)(b)<<1)|(a))
#define gDPPipeSync(g) ((void)0)
#define gDPSetCycleType(g,t) ((void)0)
static uint32_t source_fill_color;
#define gDPSetFillColor(g,c) (source_fill_color=(c))
static void source_rect(int32_t x,int32_t y,int32_t x2,int32_t y2) {
    if(x<INT16_MIN || x>INT16_MAX || y<INT16_MIN || y>INT16_MAX ||
       x2<x || x2>INT16_MAX || y2<y || y2>INT16_MAX){m->fault=1;return;}
    fzx_machine_menu_draw_t *d=source_packet(FZX_MACHINE_DRAW_RECT);if(!d)return;
    d->x=(int16_t)x;d->y=(int16_t)y;d->x2=(int16_t)x2;d->y2=(int16_t)y2;
    d->r=(uint8_t)(((source_fill_color>>11)&31)*255/31);
    d->g=(uint8_t)(((source_fill_color>>6)&31)*255/31);
    d->b=(uint8_t)(((source_fill_color>>1)&31)*255/31);d->a=255;
}
#define gDPFillRectangle(g,x,y,x2,y2) source_rect(x,y,x2,y2)
'''


def prelude(info):
    chunks=[ADAPTER]
    for name in ('FontSet','FontSetCase','FontChar'):
        chunks.append(cartridge(declaration((DECOMP/'include/fzx_font.h').read_text(),
                      r'^typedef enum '+name+r' \{.*?^\} '+name+';',name)))
    specs=info['specs']
    chunks.append('static unk_80077D50 source_textures[]={'+','.join(
        '{%d,0,0,%d,%d,%d}'%(i,w,h,4 if fmt=='I4' else 17)
        for i,(_,w,h,fmt) in enumerate(specs))+'};\n')
    chunks.extend('#define %s (&source_textures[%d])\n'%(name,i) for name,i in info['aliases'].items())
    for number in (1,2):
        font=info['fonts'][number]
        chunks.append('static unk_80077D50 source_glyphs%d[]={'%number+','.join(
            '{%d,1,%d,%d,%d,%d}'%(i,number,specs[image][1],specs[image][2],4 if specs[image][3]=='I4' else 17)
            for i,image in enumerate(font['images']))+'};\n')
        chunks.append('static unk_80077D50 *sFont%dCompTexInfos[]={'%number+','.join(
            '&source_glyphs%d[%d]'%(number,i) for i in range(len(font['images'])))+'};\n')
    for name,ids in info['tables'].items():
        if name.startswith('sFont'):continue
        if name in ('sPortraitCompTexInfos','sSmallPortraitCompTexInfos','sNameCardCompTexInfos',
                    'sTrophyCompTexInfos','sDifficultyCompTexInfos'):continue
        if name.startswith('D_'):
            chunks.append('static const uint16_t '+name+'[]={'+','.join(map(str,ids))+'};\n')
        else:
            chunks.append('static unk_80077D50 *'+name+'[]={'+','.join('&source_textures[%d]'%i for i in ids)+'};\n')
    for number in (3,4,5,6):
        chunks.append('#define sFont%dCompTexInfos sFont1CompTexInfos\n'%number)
    chunks.append(r'''
static int32_t func_800792D8(uint16_t image) {
    if(m->cache_count>=16 || image>=ARRAY_COUNT(source_textures)){m->fault=1;return 0;}
    int32_t index=m->cache_count++;m->cache_images[index]=image;return index;
}
static void func_800793E8(int32_t index,int32_t frame,uint16_t image) {
    (void)frame;
    if(index<0 || index>=m->cache_count || image>=ARRAY_COUNT(source_textures)){m->fault=1;return;}
    m->cache_images[index]=image;
}
static Gfx *source_cache_emit(Gfx *g,const uint16_t *image,int32_t x,int32_t y,unsigned mode,
                              int32_t a,int32_t b,float sx,float sy) {
    if(!image || *image>=ARRAY_COUNT(source_textures)){m->fault=1;return g;}
    return source_emit(g,&source_textures[*image],x,y,mode,a,b,sx,sy);
}
#define func_80078F80_impl(g,t,x,y,mode,a,b,c,d,e) source_cache_emit(g,t,x,y,mode,a,b,c,d)
''')
    return '\n'.join(chunks)


INITS=('MachineSelect_HeaderInit','MachineSettings_PortraitInit','MachineSelect_PortraitInit',
       'MachineSelect_CursorNumInit','MachineSelect_CursorInit','MachineSettings_EngineWeightInit',
       'MachineSettings_StatsInit','MachineSettings_NameCardInit','MachineSettings_SliderInit',
       'MachineSelect_StatsInit','MachineSelect_OkInit','MachineSettings_OkInit')
DRAWS=('MachineSelect_BackgroundDraw','MachineSelect_HeaderDraw','MachineSettings_PortraitDraw',
       'MachineSelect_StatsDraw','MachineSelect_PortraitDraw','MachineSelect_CursorNumDraw',
       'MachineSelect_CursorDraw','MachineSelect_OkDraw','MachineSettings_NameDraw',
       'MachineSettings_EngineWeightDraw','MachineSettings_StatsDraw','MachineSettings_NameCardDraw',
       'MachineSettings_SliderDraw','MachineSelect_NameDraw','MachineSettings_OkDraw')
UPDATES=('MachineSettings_PortraitUpdate','MachineSelect_PortraitUpdate','MachineSelect_CursorNumUpdate',
         'MachineSelect_OkUpdate','MachineSettings_OkUpdate','MachineSettings_NameCardUpdate')


def fragments(source):
    font=cartridge(FONT.read_text());common=cartridge(COMMON.read_text());chunks=[]
    for number in range(1,7):
        name='kFont%dCharWidths'%number
        chunks.append('static '+declaration(font,r'^const s16 '+name+r'\[\] = \{.*?^\};',name))
    chunks.append('static '+declaration(font,r'^const s16 kMachineWeightDigitWidths\[\] = \{.*?\};','weight widths'))
    for name in ('func_i2_80105EE4','func_i2_80105F48','func_i2_80105F6C','Font_GetCharIndex',
                 'Font_GetStringWidth','Font_DrawString','Font_DrawMachineStatValue',
                 'Font_DrawMachineWeight','Font_DrawMachineWeightSmall'):
        body=function(font,name).replace('s8* str','const s8* str')
        if name=='Font_DrawMachineStatValue':
            guard='(statValue < 0) && (statValue >= 5)'
            if body.count(guard)!=1:raise ArtError('Original stat validation changed')
            # Original impossible conjunction was a bounds-check bug. Valid 0..4
            # source values are identical; reject invalid indexes at this boundary.
            body=body.replace(guard,'(statValue < 0) || (statValue >= 5)')
            body=body.replace('{','{\n    /* Bounded adaptation: fix the original impossible bounds conjunction; valid stats unchanged. */',1)
        if name in ('Font_GetStringWidth','Font_DrawString'):
            result='0' if name=='Font_GetStringWidth' else 'gfx'
            body=body.replace('{','{\n    if(fontSet!=FONT_SET_1 && fontSet!=FONT_SET_2){m->fault=1;return '+result+';}',1)
        chunks.append(body)
    chunks.append('static '+declaration(common,r'^s32 D_800CD4E0\[\] = \{.*?^\};','pulse palette'))
    chunks.append(function(common,'func_8007DB28'))
    chunks.append('static '+declaration(common,r'^const char\* gMachineNames\[\] = \{.*?\};','machine names'))
    racer=RACER.read_text()
    machines=declaration(racer,r'^Machine sDefaultMachines\[\] = \{.*?^\};','default machines')
    stats=re.findall(r'\{\s*BODY_([A-E]),\s*BOOST_([A-E]),\s*GRIP_([A-E])\s*\},\s*(\d+)\s*\}',machines)
    if len(stats)!=33:raise ArtError('Original machine stats extent changed')
    # Preserve source enum definitions as well as their original initializers.
    header=(DECOMP/'include/fzx_racer.h').read_text()
    for name in ('BodyStat','BoostStat','GripStat'):
        chunks.append(declaration(header,r'^typedef enum '+name+r' \{.*?^\} '+name+';',name))
    chunks.append('typedef struct { int16_t weight; s8 machineStats[3]; } Machine;\n')
    chunks.append('static Machine gMachines[]={'+','.join('{%s,{BODY_%s,BOOST_%s,GRIP_%s}}'%(w,a,b,c)
                  for a,b,c,w in stats[:30])+'};\n')
    for name in ('D_i4_8011D674','D_i4_8011D694','D_i4_8011D6C4'):
        chunks.append('static '+declaration(source,r'^const s32 '+name+r'\[\] = \{.*?\};',name))
    chunks.append(declaration(source,r'^static const char\* sMachineStatValueStr\[\] = \{.*?\};','stat labels'))
    chunks.append(function(source,'MachineSettings_GetCharacter'))
    for name in ('Object_LerpPosXToClampedTargetMaxStep','Object_LerpPosYToClampedTarget'):
        chunks.append(function(OBJECT.read_text(),name))
    background=function(cartridge(TEXTURE.read_text()),'func_8007AC48')
    # Unsigned source arguments cannot be negative; remove only redundant tests.
    for channel in ('red','green','blue'):
        background=background.replace('('+channel+' < 0) || ','')
    chunks.append(background)
    for name in (*INITS,*DRAWS,*UPDATES):
        body=function(source,name)
        # Mark stock padding and inactive-branch locals without changing execution.
        for var in {'MachineSelect_BackgroundDraw':['color'],
                    'MachineSettings_NameDraw':['characterSlot'],
                    'MachineSettings_StatsDraw':['pad'],
                    'MachineSelect_NameDraw':['pad']}.get(name,[]):
            match=re.search(r'^    s32 '+var+r'(?:\[2\])?;$',body,re.M)
            if not match:raise ArtError('Original unused draw local changed: '+name+' '+var)
            body=body[:match.end()]+'\n    (void)sizeof('+var+');'+body[match.end():]
        for var in {'MachineSettings_SliderDraw':['sliderObj'],'MachineSelect_NameDraw':['nameObj']}.get(name,[]):
            body=body.replace('{','{\n    (void)'+var+';',1)
        body=body.replace('sMachineStatValueStr[temp[j]]','sMachineStatValueStr[(int)temp[j]]')
        if name=='MachineSettings_SliderDraw':
            body=body.replace('                default:', '                /* fallthrough */\n                default:')
        chunks.append(body)
    chunks.append(BRIDGE)
    return '\n'.join(chunks)


BRIDGE=r'''
static Object *Object_Get(int32_t id) {
    for(unsigned i=0;i<m->object_count;++i)if(m->objects[i].cmdId==id)return &m->objects[i];
    return NULL;
}
static void Object_Init(int32_t id,int32_t left,int32_t top,int32_t priority) {
    if(m->object_count>=FZX_MACHINE_MENU_OBJECT_MAX){m->fault=1;return;}
    Object *o=&m->objects[m->object_count++];memset(o,0,sizeof(*o));
    o->cmdId=id;o->left=left;o->top=top;o->priority=(uint8_t)priority;o->shouldDraw=1;
    switch(id) {
        case OBJECT_MACHINE_SELECT_HEADER:MachineSelect_HeaderInit();break;
        case OBJECT_MACHINE_SELECT_CURSOR:MachineSelect_CursorInit();break;
        case OBJECT_MACHINE_SELECT_CURSOR_NUM_0:MachineSelect_CursorNumInit(o);break;
        case OBJECT_MACHINE_SELECT_PORTRAIT_0:MachineSelect_PortraitInit(o);break;
        case OBJECT_MACHINE_SELECT_STATS_0:MachineSelect_StatsInit();break;
        case OBJECT_MACHINE_SELECT_OK:MachineSelect_OkInit(o);break;
        case OBJECT_MACHINE_SETTINGS_PORTRAIT_0:MachineSettings_PortraitInit(o);break;
        case OBJECT_MACHINE_SETTINGS_ENGINE_WEIGHT:MachineSettings_EngineWeightInit();break;
        case OBJECT_MACHINE_SETTINGS_STATS:MachineSettings_StatsInit();break;
        case OBJECT_MACHINE_SETTINGS_NAME_CARD:MachineSettings_NameCardInit(o);break;
        case OBJECT_MACHINE_SETTINGS_SLIDER:MachineSettings_SliderInit();break;
        case OBJECT_MACHINE_SETTINGS_OK:MachineSettings_OkInit(o);break;
        case OBJECT_FRAMEBUFFER:case OBJECT_MACHINE_SELECT_BACKGROUND:case OBJECT_MACHINE_SELECT_MACHINE:
        case OBJECT_MACHINE_SELECT_NAME:case OBJECT_MACHINE_SETTINGS_BACKGROUND:
        case OBJECT_MACHINE_SETTINGS_NAME:case OBJECT_MACHINE_SETTINGS_MACHINE:break;
        default:m->fault=1;break;
    }
}
static void source_render_step(void) {
    Gfx dummy=0,*gfx=&dummy;memset(&m->snapshot,0,sizeof(m->snapshot));source_color(255,255,255,255);
    for(unsigned i=0;i<m->object_count;++i) {
        Object *o=&m->objects[i];
        switch(o->cmdId) {
            case OBJECT_MACHINE_SELECT_PORTRAIT_0:MachineSelect_PortraitUpdate(o);break;
            case OBJECT_MACHINE_SELECT_CURSOR_NUM_0:MachineSelect_CursorNumUpdate(o);break;
            case OBJECT_MACHINE_SELECT_OK:MachineSelect_OkUpdate(o);break;
            case OBJECT_MACHINE_SETTINGS_PORTRAIT_0:MachineSettings_PortraitUpdate(o);break;
            case OBJECT_MACHINE_SETTINGS_NAME_CARD:MachineSettings_NameCardUpdate(o);break;
            case OBJECT_MACHINE_SETTINGS_OK:MachineSettings_OkUpdate(o);break;
            case OBJECT_MACHINE_SELECT_MACHINE:case OBJECT_MACHINE_SETTINGS_MACHINE:++m->model_ticks;break;
            default:break;
        }
    }
    /* Original Object_UpdateAndDrawAll: update allocation order, draw stable priority order. */
    for(unsigned p=0;p<16;++p)for(unsigned i=0;i<m->object_count;++i) {
        Object *o=&m->objects[i];if(o->priority!=p || !o->shouldDraw)continue;
        switch(o->cmdId) {
            case OBJECT_MACHINE_SELECT_BACKGROUND:gfx=MachineSelect_BackgroundDraw(gfx);break;
            case OBJECT_MACHINE_SETTINGS_BACKGROUND:gfx=func_8007AC48(gfx,24,24,24);break;
            case OBJECT_MACHINE_SELECT_HEADER:gfx=MachineSelect_HeaderDraw(gfx,o);break;
            case OBJECT_MACHINE_SELECT_CURSOR:gfx=MachineSelect_CursorDraw(gfx,o);break;
            case OBJECT_MACHINE_SELECT_CURSOR_NUM_0:gfx=MachineSelect_CursorNumDraw(gfx,o);break;
            case OBJECT_MACHINE_SELECT_PORTRAIT_0:gfx=MachineSelect_PortraitDraw(gfx,o);break;
            case OBJECT_MACHINE_SELECT_STATS_0:gfx=MachineSelect_StatsDraw(gfx,o);break;
            case OBJECT_MACHINE_SELECT_NAME:gfx=MachineSelect_NameDraw(gfx,o);break;
            case OBJECT_MACHINE_SELECT_OK:gfx=MachineSelect_OkDraw(gfx,o);break;
            case OBJECT_MACHINE_SETTINGS_PORTRAIT_0:gfx=MachineSettings_PortraitDraw(gfx,o);break;
            case OBJECT_MACHINE_SETTINGS_NAME:gfx=MachineSettings_NameDraw(gfx);break;
            case OBJECT_MACHINE_SETTINGS_ENGINE_WEIGHT:gfx=MachineSettings_EngineWeightDraw(gfx,o);break;
            case OBJECT_MACHINE_SETTINGS_STATS:gfx=MachineSettings_StatsDraw(gfx,o);break;
            case OBJECT_MACHINE_SETTINGS_NAME_CARD:gfx=MachineSettings_NameCardDraw(gfx,o);break;
            case OBJECT_MACHINE_SETTINGS_SLIDER:gfx=MachineSettings_SliderDraw(gfx,o);break;
            case OBJECT_MACHINE_SETTINGS_OK:gfx=MachineSettings_OkDraw(gfx,o);break;
            case OBJECT_MACHINE_SELECT_MACHINE:case OBJECT_MACHINE_SETTINGS_MACHINE:
                (void)source_packet(FZX_MACHINE_DRAW_MODELS);break;
            default:break;
        }
    }
}
'''
