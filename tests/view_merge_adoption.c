/* Synthetic port-side caller contract; no course data. */
#include "sh2zam/view_experimental.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"merge caller line %u\n",(unsigned)__LINE__);return 1; } } while(0)

int main(void) {
    sh2z_view_vertex_t left[9]={{0}},right[9]={{0}};
    uint32_t rows[9],ends[9]={0};
    struct {uint8_t runs[8];uint32_t guard;} out;
    for(unsigned j=0;j<9;++j) {
        rows[j]=j*32u;left[j].sx=10;right[j].sx=110;
        left[j].sy=right[j].sy=(int16_t)(20+j*3);
        if(j>=2)ends[j]|=1u<<2;
        if(j>=4)ends[j]|=1u<<4;
    }
    memset(&out,0xa5,sizeof(out));
    CHECK(sh2z_view_merge_runs(out.runs,left,right,rows,ends,9,0x16,256,SH2Z_VIEW_MERGE_PIXELS)==2);
    CHECK(out.runs[0]==4 && out.runs[1]==4 && out.runs[2]==0xa5 && out.guard==0xa5a5a5a5u);
    /* Block one endpoint of both four-runs, including side-plane vertices
     * the caller will clip. Side outcodes alone do not block the library. */
    left[4].outcode=SH2Z_OUT_LEFT;
    CHECK(sh2z_view_merge_runs(out.runs,left,right,rows,ends,9,0x16,256,0)==2);
    left[4].outcode|=0x80;
    CHECK(sh2z_view_merge_runs(out.runs,left,right,rows,ends,9,0x16,256,0)==6);
    CHECK(out.runs[0]==2 && out.runs[1]==1 && out.runs[2]==1 && out.runs[3]==1 && out.runs[4]==2 && out.runs[5]==1);
    left[4].outcode=0;
    /* A half-period start and fractional last period stay single. Rows are
     * source Q5 units rebased to unsigned; the error ratio is scale invariant. */
    const uint32_t partial[7]={16,32,64,96,128,160,164};
    memset(ends,0,sizeof(ends));
    for(unsigned j=0;j<7;++j) {
        rows[j]=partial[j]*32u;
        left[j].sy=right[j].sy=(int16_t)partial[j];
        for(unsigned run=2;run<=4;run*=2)
            if(j>=run && partial[j-run]%32u==0 && partial[j]-partial[j-run]==32u*run)
                ends[j]|=1u<<run;
    }
    CHECK(sh2z_view_merge_runs(out.runs,left,right,rows,ends,7,0x16,256,0)==3);
    CHECK(out.runs[0]==1 && out.runs[1]==4 && out.runs[2]==1);
    uint8_t scaled[8];for(unsigned j=0;j<7;++j)rows[j]=partial[j]-partial[0];
    CHECK(sh2z_view_merge_runs(scaled,left,right,rows,ends,7,0x16,256,0)==3);
    CHECK(!memcmp(scaled,out.runs,3));
    /* Invalid input must not touch output or input. */
    uint8_t saved[sizeof(out)];memcpy(saved,&out,sizeof(out));rows[3]=rows[2];
    CHECK(sh2z_view_merge_runs(out.runs,left,right,rows,ends,7,0x16,256,0)==-1);
    CHECK(!memcmp(saved,&out,sizeof(out)));
    puts("merge caller: tier alignment, clipped endpoints, Q5 row scaling, output guards and rejection PASS");
    return 0;
}
