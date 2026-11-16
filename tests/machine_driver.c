#include "fzx_machine.h"
#include <stdio.h>
#include <string.h>
#define CHECK(C) do{if(!(C)){fprintf(stderr,"machine check line %d\n",__LINE__);return 1;}}while(0)
static uint32_t random_state=314159u;
static uint16_t random16(void){random_state=random_state*1664525u+1013904223u;return (uint16_t)(random_state>>16);}
int main(void)
{
    const uint8_t fog[3]={192,65,19};
    const fzx_material_combine_t identity={{255,255,255},{0,0,0},false};
    fzx_material_color_t base[16],shade[3];fzx_machine_palette_t out;
    for(unsigned trial=0;trial<1000;++trial){
        for(unsigned i=0;i<16;++i)base[i]=(fzx_material_color_t){random16(),random16(),random16(),65535};
        shade[0]=(fzx_material_color_t){random16(),random16(),random16(),random16()};shade[1]=shade[2]=shade[0];
        CHECK(fzx_machine_light(base,16,shade,fog,&out));
        fzx_machine_palette_t reused;
        CHECK(fzx_machine_light_reuse(base,16,shade,fog,&reused));CHECK(!memcmp(&out,&reused,sizeof(out)));
        for(unsigned i=0;i<16;++i)CHECK(out.palette[i]==fzx_material_pixel(&identity,base[i],shade[0],fog));
        CHECK(out.saturated_channels==0);
        for(unsigned i=0;i<4;++i)CHECK(out.gouraud[i]==(16u|(16u<<5)|(16u<<10)));
    }
    base[0]=(fzx_material_color_t){65535,65535,65535,65535};
    shade[0]=(fzx_material_color_t){0,0,0,0};shade[1]=(fzx_material_color_t){32768,32768,32768,0};
    shade[2]=(fzx_material_color_t){65535,65535,65535,0};
    CHECK(fzx_machine_light(base,1,shade,fog,&out));CHECK(out.saturated_channels==3);
    fzx_machine_palette_t mixed;CHECK(fzx_machine_light_reuse(base,1,shade,fog,&mixed));CHECK(!memcmp(&out,&mixed,sizeof(out)));
    CHECK(out.gouraud[0]==(1u|(1u<<5)|(1u<<10)));CHECK(out.gouraud[2]==0x7fff);CHECK(out.gouraud[3]==out.gouraud[2]);
    CHECK(!fzx_machine_light(base,0,shade,fog,&out));CHECK(!fzx_machine_light(base,17,shade,fog,&out));
    CHECK(!fzx_machine_light(NULL,1,shade,fog,&out));
    uint8_t lut[256];for(unsigned i=0;i<256;++i)lut[i]=(uint8_t)i;
    fzx_machine_pose_t pose={{{16384,0,0},{0,16384,0},{0,0,16384}},{0,0,16384},{0,0,-16384},1200,75};
    fzx_machine_vertex_t v={{0,75,0},{33,-32},{0,0,127}};fzx_model_vertex_t p;
    CHECK(fzx_machine_project(&v,1,&pose,lut,&p));
    CHECK(p.x_q4==1024&&p.y_q4==1024&&p.inverse_z==13981&&p.shade.r==65535&&p.shade.a==257);
    CHECK(p.u_q5==33&&p.v_q5==-32);
    v.position[0]=60;pose.rotation[0][0]=pose.rotation[1][1]=0;pose.rotation[0][1]=-16384;pose.rotation[1][0]=16384;
    CHECK(fzx_machine_project(&v,1,&pose,lut,&p));CHECK(p.x_q4==1024&&p.y_q4==896);
    fzx_machine_vertex_t duplicates[4]={v,v,v,v};
    duplicates[1].uv[0]=192;duplicates[2].position[0]=20;duplicates[3].normal[2]=-127;
    const uint16_t ps[]={0,2},ns[]={0,3},pm[]={0,0,1,0};uint16_t nm[]={0,0,0,1};
    const fzx_machine_plan_t plan={ps,ns,pm,nm,4,2,2};
    fzx_machine_position_t pc[2];fzx_material_color_t nc[2];fzx_model_vertex_t standard[4],reused[4];
    CHECK(fzx_machine_plan_valid(duplicates,&plan));
    CHECK(fzx_machine_project(duplicates,4,&pose,lut,standard));
    CHECK(fzx_machine_project_reuse(duplicates,&plan,&pose,lut,pc,nc,reused));
    CHECK(!memcmp(standard,reused,sizeof(standard)));
    nm[3]=0;CHECK(!fzx_machine_plan_valid(duplicates,&plan));nm[3]=2;CHECK(!fzx_machine_plan_valid(duplicates,&plan));nm[3]=1;
    pose.distance=500;CHECK(!fzx_machine_project(&v,1,&pose,lut,&p));pose.distance=1200;
    v.position[0]=1025;CHECK(!fzx_machine_project(&v,1,&pose,lut,&p));
    /* Exercise allowed extreme coordinates/matrix components under sanitizers. */
    for(unsigned trial=0;trial<10000;++trial){
        for(unsigned j=0;j<3;++j){v.position[j]=(int16_t)((int)(random16()%2049)-1024);v.normal[j]=(int8_t)(random16()%256-128);
            for(unsigned k=0;k<3;++k)pose.rotation[j][k]=(int16_t)((int)(random16()%32769)-16384);}
        (void)fzx_machine_project(&v,1,&pose,lut,&p);
    }
    puts("MACHINE palette oracle, projection and bounds PASS");return 0;
}
