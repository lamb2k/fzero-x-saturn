"""Bounded source results draw capture; no display-list execution at runtime.

Only the named HUD sheets, Font3, two setup lists and solid rectangles occur
in this closure. Capture runs once per logic tick. Consumers redraw its value
packets without rerunning source code or touching its timers.
"""
import re
from import_source_art import DECOMP, YAML, ArtError, metadata
from menu_source import function
from course_menu_source import declaration
from course_menu_assets import font_tables
from hud_assets import HUD_SHEETS

FONT = DECOMP/'src/overlays/ovl_i2/font.c'
HUD = DECOMP/'src/overlays/ovl_i3/hud.c'
TEXTURE = DECOMP/'src/game/texture_utils2.c'
MATH = DECOMP/'src/sys/math.c'


def dependencies():
    return [FONT, HUD, TEXTURE, MATH, DECOMP/'include/fzx_font.h',
            DECOMP/'include/fzx_hud.h', DECOMP/YAML/'common_assets_compressed.yaml']


ADAPTER = r'''
typedef struct { uint16_t index,width,height; } unk_80077D50;
static uint8_t draw_color[4],draw_copy,draw_primitive,draw_modulate;
static int32_t draw_image,draw_width,draw_height,draw_clip[4];
static void source_color(int32_t r,int32_t g,int32_t b,int32_t a) {
    draw_color[0]=(uint8_t)r;draw_color[1]=(uint8_t)g;
    draw_color[2]=(uint8_t)b;draw_color[3]=(uint8_t)a;
}
static fzx_results_draw_t *source_packet(uint8_t kind) {
    if(m->snapshot.count>=FZX_RESULTS_DRAW_MAX){m->fault=1;return NULL;}
    fzx_results_draw_t *p=&m->snapshot.draws[m->snapshot.count++];
    memset(p,0,sizeof(*p));p->kind=kind;
    p->clip_x=(int16_t)draw_clip[0];p->clip_y=(int16_t)draw_clip[1];
    p->clip_x2=(int16_t)draw_clip[2];p->clip_y2=(int16_t)draw_clip[3];
    if(draw_primitive || draw_modulate || kind==FZX_RESULTS_GLYPH) {
        p->r=draw_color[0];p->g=draw_color[1];p->b=draw_color[2];p->a=draw_color[3];
    } else p->r=p->g=p->b=p->a=255;
    return p;
}
/* The two pinned setup lists select 1CYCLE and COPY respectively. Their
 * point filtering and absent perspective need no packet-time approximation. */
#define D_8014940 0
#define D_80149D0 1
static void source_setup(int32_t setup) {
    if(setup!=0 && setup!=1){m->fault=1;return;}
    draw_copy=(uint8_t)setup;draw_primitive=draw_modulate=0;
}
#define G_CC_PRIMITIVE 1
#define G_CC_MODULATEIDECALA_PRIM 2
static void source_combine(int32_t mode) {
    if(mode!=G_CC_PRIMITIVE && mode!=G_CC_MODULATEIDECALA_PRIM){m->fault=1;return;}
    draw_primitive=(uint8_t)(mode==G_CC_PRIMITIVE);
    draw_modulate=(uint8_t)(mode==G_CC_MODULATEIDECALA_PRIM);
}
static void source_scissor(int32_t x,int32_t y,int32_t x2,int32_t y2) {
    if(x<0 || y<0 || x2<x || y2<y || x2>320 || y2>240){m->fault=1;return;}
    draw_clip[0]=x;draw_clip[1]=y;draw_clip[2]=x2;draw_clip[3]=y2;
}
static void source_texture(int32_t image,int32_t width,int32_t height) {
    if(image<0 || image>=8 || width!=source_sheet_sizes[image][0] ||
       height!=source_sheet_sizes[image][1]){m->fault=1;return;}
    draw_image=image;draw_width=width;draw_height=height;
}
static void source_rectangle(int32_t xq,int32_t yq,int32_t x2q,int32_t y2q,
                             int32_t sq,int32_t tq,int32_t dx,int32_t dy) {
    if((xq%4) || (yq%4) || (x2q%4) || (y2q%4) || (sq%32) || (tq%32) ||
       dy!=1024 || dx!=(draw_copy?4096:1024)){m->fault=1;return;}
    int32_t x=xq/4,y=yq/4,x2=x2q/4+draw_copy,y2=y2q/4+draw_copy;
    int32_t sx=sq/32,sy=tq/32,w=x2-x,h=y2-y;
    if(x<INT16_MIN || y<INT16_MIN || x2>INT16_MAX || y2>INT16_MAX || w<0 || h<0 ||
       (!draw_primitive && (sx<0 || sy<0 || sx+w>draw_width || sy+h>draw_height))) {
        m->fault=1;return;
    }
    /* Whole invisible commands do not consume native command capacity. Keep
     * unclipped coordinates/crops for visible commands; clipping is a consumer. */
    if(x2<=draw_clip[0] || y2<=draw_clip[1] || x>=draw_clip[2] || y>=draw_clip[3] || !w || !h)return;
    fzx_results_draw_t *p=source_packet(draw_primitive?FZX_RESULTS_RECT:FZX_RESULTS_IMAGE);
    if(!p)return;
    p->x=(int16_t)x;p->y=(int16_t)y;p->x2=(int16_t)x2;p->y2=(int16_t)y2;
    p->sx=(int16_t)sx;p->sy=(int16_t)sy;p->w=(int16_t)w;p->h=(int16_t)h;
    if(!draw_primitive)p->image=(uint16_t)draw_image;
}
static Gfx *source_glyph(Gfx *gfx,const unk_80077D50 *glyph,int32_t x,int32_t y,
                         int32_t mode,int32_t flip_x,int32_t flip_y,float sx,float sy) {
    if(!glyph || mode || flip_x || flip_y || sx!=1.0f || sy!=1.0f ||
       x<INT16_MIN || x>INT16_MAX-glyph->width || y<INT16_MIN || y>INT16_MAX-glyph->height) {
        m->fault=1;return gfx;
    }
    if(x+glyph->width<=draw_clip[0] || y+glyph->height<=draw_clip[1] ||
       x>=draw_clip[2] || y>=draw_clip[3])return gfx;
    fzx_results_draw_t *p=source_packet(FZX_RESULTS_GLYPH);if(!p)return gfx;
    p->image=glyph->index;p->x=(int16_t)x;p->y=(int16_t)y;
    p->w=(int16_t)glyph->width;p->h=(int16_t)glyph->height;
    p->x2=(int16_t)(x+glyph->width);p->y2=(int16_t)(y+glyph->height);
    return gfx;
}
#define gSPDisplayList(g,list) source_setup(list)
#define gDPPipeSync(g) ((void)0)
#define gDPSetAlphaCompare(g,a) ((void)0)
#define gDPSetPrimColor(g,l,d,r,b,c,a) source_color(r,b,c,a)
#define gDPSetCombineMode(g,a,b) source_combine(a)
#define gDPSetScissor(g,m,x,y,x2,y2) source_scissor(x,y,x2,y2)
#define gDPLoadTextureBlock(g,tex,fmt,siz,w,h,p,cs,ct,ms,mt,ls,lt) source_texture(tex,w,h)
#define gSPScisTextureRectangle(g,x,y,x2,y2,tile,s,t,dx,dy) source_rectangle(x,y,x2,y2,s,t,dx,dy)
#define gSPTextureRectangle(g,x,y,x2,y2,tile,s,t,dx,dy) source_rectangle(x,y,x2,y2,s,t,dx,dy)
#define func_80077D50_impl(t,x,y) ((void)(t))
#define func_80078EA0_impl(g,t,x,y,mode,a,b,c,d,e) source_glyph(g,t,x,y,mode,a,b,c,d)
'''


def fragments(menus, select_us):
    font=select_us(FONT.read_text());chunks=[]
    sheets=(*HUD_SHEETS,('aMaxSpeedTex',64,16))
    chunks.append('static const int32_t source_sheet_sizes[8][2]={'+','.join(
        '{%d,%d}'%(w,h) for _,w,h in sheets)+'};')
    chunks.extend('#define %s %d'%(name,i) for i,(name,_,_) in enumerate(sheets))
    chunks.append(ADAPTER)
    for name in ('FontSet','FontSetCase','FontChar'):
        chunks.append(select_us(declaration((DECOMP/'include/fzx_font.h').read_text(),
                    r'^typedef enum '+name+r' \{.*?^\} '+name+';',name)))
    chunks.append(declaration((DECOMP/'include/fzx_hud.h').read_text(),
                             r'^typedef enum TimerDigits \{.*?^\} TimerDigits;','timer digits'))
    table=font_tables(FONT.read_text())[3]
    rows=metadata((DECOMP/YAML/'common_assets_compressed.yaml').read_text())
    glyphs=[]
    for i,symbol in enumerate(table['symbols']):
        row=rows.get(symbol,{})
        if row.get('format')!='I4' or row.get('height')!=16 or not 8<=row.get('width',0)<=128:
            raise ArtError('Source Font3 glyph extent changed: '+symbol)
        glyphs.append('{%d,%d,%d}'%(i,row['width'],row['height']))
    chunks.append('static unk_80077D50 source_glyphs[]={'+','.join(glyphs)+'};')
    chunks.append('static unk_80077D50 *sFont3CompTexInfos[]={'+','.join(
        '&source_glyphs[%d]'%i for i in range(len(glyphs)))+'};')
    for number in range(1,7):
        if number!=3:
            chunks.append('#define sFont%dCompTexInfos sFont3CompTexInfos'%number)
        chunks.append('static '+declaration(font,r'^const s16 kFont%dCharWidths\[\] = \{.*?^\};'%number,'font widths'))
    for name in ('func_i2_80105EE4','func_i2_80105F48','func_i2_80105F6C','Font_GetCharIndex',
                 'Font_GetStringWidth','Font_DrawString'):
        body=function(font,name).replace('s8* str','const s8* str')
        if name in ('Font_GetStringWidth','Font_DrawString'):
            result='0' if name=='Font_GetStringWidth' else 'gfx'
            body=body.replace('{','{\n    if(fontSet!=FONT_SET_3){m->fault=1;return '+result+';}',1)
        chunks.append(body)
    chunks.append(select_us(function(MATH.read_text(),'Math_Round')))
    chunks.append(function(TEXTURE.read_text(),'func_8007F090'))
    hud=select_us(HUD.read_text())
    for name in ('Hud_DrawTimerDigitScisRectangle','Hud_DrawTimerScisThousandths'):
        chunks.append(function(hud,name))
    for name in ('Menus_DrawBeveledBox','Menus_DrawBox','Menus_DrawRaceResultsTimes',
                 'Menu_DrawTimeAttackResultsTimes','Menus_DrawMaxSpeed','Menus_DrawCourseName'):
        chunks.append(function(menus,name))
    # Original signed left shifts can receive negative off-screen coordinates.
    # Multiplication is the same bounded value without C's negative-shift UB.
    result='\n'.join(chunks).replace(' << 2',' * 4')
    return result
