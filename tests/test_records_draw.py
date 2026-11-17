"""Synthetic results crops and clipping, with independently checked texel coordinates."""
from pathlib import Path
import os,shlex,shutil,subprocess,tempfile,unittest,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class RecordsDraw(unittest.TestCase):
    @unittest.skipUnless((ROOT/'upstream/G-Diffuser/decomp/src/game/camera.c').is_file(),'pinned source required')
    def test_spinner_source_projection(self):
        import records_draw_source as draw
        source=draw.camera_capture_source().decode().split('int main(void)',1)[0]
        driver=r'''
#include "sh2zam/view_experimental.h"
#include <assert.h>
int main(void) {
    Math_SinTableInit();unsigned samples=0,max_error=0;
    for(unsigned frame=0;frame<128;++frame)for(int width=8;width<=16;width+=8) {
        fzx_records_draw_snapshot_t packet={0};packet.frame=frame;
        packet.spinner.visible=1;packet.spinner.width=(int16_t)width;packet.spinner.height=16;
        packet.spinner.x=188;packet.spinner.y=132;
        int32_t q[18];assert(fzx_records_spinner_matrix(&packet,q));
        Mtx m,v,p;MtxF mf,vf,pf,combined;
        source_spinner_matrices(frame,&m,&mf,&v,&vf,&p,&pf);
        Matrix_AffineMultiply(&vf,&mf,&combined);
        sh2z_view_t view={0};
        for(unsigned i=0;i<3;++i) {
            for(unsigned j=0;j<3;++j)view.r[i][j]=(int16_t)((i==2?-q[j*4+i]:q[j*4+i])/4);
            view.t[i]=(i==2?-q[12+i]:q[12+i])/4;
        }
        view.focal_x=q[16]*320;view.focal_y=q[17]*240;
        view.center_x=packet.spinner.x;view.center_y=packet.spinner.y-8;
        view.near_z=16*16384;view.far_z=8129*16384;
        view.clip[0]=0;view.clip[1]=0;view.clip[2]=319;view.clip[3]=223;
        sh2z_pos16_t vertices[4]={{-width/2,8,0},{width/2,8,0},{-width/2,-8,0},{width/2,-8,0}};
        sh2z_view_vertex_t projected[4];
        assert(sh2z_view_batch_trunc_divu(projected,&view,vertices,sizeof(vertices[0]),4));
        for(unsigned k=0;k<4;++k) {
            double xyz[3];
            for(unsigned j=0;j<3;++j)xyz[j]=(double)vertices[k].x*combined.m[0][j]+(double)vertices[k].y*combined.m[1][j]+combined.m[3][j];
            int expected[2]={(int)(188.0-pf.m[0][0]*320.0*xyz[0]/xyz[2]),
                             (int)(124.0+pf.m[1][1]*240.0*xyz[1]/xyz[2])};
            int error_x=abs(projected[k].sx-expected[0]),error_y=abs(projected[k].sy-expected[1]);
            assert(error_x<=1&&error_y<=1);
            if((unsigned)error_x>max_error)max_error=(unsigned)error_x;
            if((unsigned)error_y>max_error)max_error=(unsigned)error_y;
            ++samples;
        }
    }
    printf("spinner source projection %u vertices maximum%upx PASS\n",samples,max_error);return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='fzx-records-matrix-') as td:
            temp=Path(td)
            def run(body,cc):
                (temp/'driver.c').write_text(source+'\n#include <stdlib.h>\n'+body)
                command=[cc,'-std=c11','-O2','-ffp-contract=off','-fno-strict-aliasing',
                    '-DSH2Z_FORCE_PORTABLE=1','-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(temp/'driver.c'),str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view.c'),
                    str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view_round.c'),'-lm','-o',str(temp/'test')]
                built=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(built.returncode,0,built.stderr)
                return subprocess.run([str(temp/'test')],capture_output=True,text=True)
            for cc in ('gcc','clang'):
                got=run(driver,cc);self.assertEqual(got.returncode,0,got.stderr);print(got.stdout.strip())
            for before,after in [('view.focal_x=q[16]*320','view.focal_x=q[16]*160'),
                ('view.center_y=packet.spinner.y-8','view.center_y=packet.spinner.y'),
                ('view.t[i]=(i==2?-q[12+i]:q[12+i])/4;',
                 'view.t[i]=(i==2?-q[12+i]:q[12+i])/4;for(unsigned j=0;j<3;++j)view.r[i][j]/=2;')]:
                with self.subTest(control=before):
                    got=run(driver.replace(before,after),'gcc')
                    self.assertNotEqual(got.returncode,0,'Spinner control survived')

    def test_crops_controls_and_compilers(self):
        outputs=[]
        with tempfile.TemporaryDirectory() as temp:
            for cc in ('gcc','clang'):
                exe=Path(temp)/cc
                subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),
                    '-I'+str(ROOT/'include'),str(ROOT/'tests/records_draw_driver.c'),
                    str(ROOT/'src/fzx_records_draw.c'),'-o',str(exe)],check=True,capture_output=True)
                outputs.append(subprocess.check_output([str(exe)]))
        self.assertEqual(outputs,[b'UI_CROP_PIXELS_AND_REJECTIONS_PASS\n']*2)

    @unittest.skipUnless((ROOT/'upstream/G-Diffuser/decomp/src/game/camera.c').is_file(),'pinned source required')
    def test_original_draw_corpus(self):
        import records_draw_source as draw
        import records_source as records
        import results_source as results
        from records_assets import catalog
        assets=catalog();rows=[];offset=0
        for name,w,h,fmt in assets['specs']:
            w=assets['crops'].get(name,w)
            width=(w+7)&~7;clut=int(fmt=='I4');size=width*h*(1 if clut else 4)//2
            rows.append('{.offset=%d,.bytes=%d,.width=%d,.height=%d,.source_width=%d,.clut=%d}'%(offset,size,width,h,w,clut));offset+=size
        fixture='static const fzx_results_image_t images[]={'+','.join(rows)+'};\n'
        # A local admitted bank strengthens the residency/crop check; CI still
        # exercises the same source commands against format-correct descriptors.
        identity=ROOT/'generated/records_ui/records_identity.h'
        if identity.is_file():
            fixture='#include "'+str(identity)+'"\n#define images fzx_records_images\n'
            offset='FZX_RECORDS_UI_BYTES'
        harness=(ROOT/'tests/records_state_driver.c').read_text()
        harness=harness.replace('#include "fzx_records.h"','#include "fzx_records_draw.h"')
        marker='static int pure_snapshot(void) {'
        extra=r'''
static fzx_records_draw_snapshot_t picture,picture_again;
static unsigned pictures,peak_ui,peak_clut,cluts;
static bool collect(void *ctx,const fzx_results_sprite_t *p) {
    (void)ctx;cluts+=p->clut;return true;
}
static int capture(void) {
    fzx_records_snapshot_t original=shot;
    CHECK(fzx_records_draw_capture(&shot,&picture));
    CHECK(fzx_records_draw_capture(&shot,&picture_again));
    CHECK(!memcmp(&picture,&picture_again,sizeof(picture)));
    CHECK(!memcmp(&original,&shot,sizeof(shot)));
    CHECK(picture.tick==shot.tick&&picture.frame==shot.frame);
    unsigned count=0;cluts=0;
    CHECK(fzx_records_ui(&picture,images,sizeof(images)/sizeof(images[0]),BYTES,collect,NULL,&count));
    if(count>peak_ui)peak_ui=count;if(cluts>peak_clut)peak_clut=cluts;
    ++pictures;
    if(shot.draw_kind==FZX_RECORDS_DRAW_NAME&&shot.draw_name_state==NAME_ENTRY_KEYBOARD) {
        CHECK(picture.spinner.visible==1&&picture.machine_count==0);
        CHECK(picture.spinner.x==shot.spinner_x+8);
        CHECK(picture.spinner.y==shot.spinner_y-8);
        CHECK(picture.spinner.width==8||picture.spinner.width==16);
    } else CHECK(!picture.spinner.visible);
    if(shot.draw_kind==FZX_RECORDS_DRAW_TABLE) {
        unsigned expected=0;
        for(unsigned i=0;i<5;++i)if(shot.records.timeRecord[i]!=MAX_TIMER) {
            const fzx_records_machine_draw_t *p=&picture.machines[expected++];
            CHECK(p->slot==i+1&&p->character==shot.records.recordMachineInfos[i].character);
            CHECK(p->r==shot.records.recordMachineInfos[i].bodyR);
            CHECK(p->x==((i&1)?-shot.record_left_offset:shot.record_left_offset)+shot.record_left+45);
        }
        if(shot.records.maxSpeed!=0)CHECK(picture.machines[expected++].slot==6);
        CHECK(expected==picture.machine_count);
    }
    return 0;
}
'''.replace('BYTES',str(offset))
        harness=harness.replace(marker,fixture+extra+'\n'+marker)
        harness=harness.replace('    CHECK(fzx_records_snapshot(&state,&shot));++reads;',
                                '    CHECK(fzx_records_snapshot(&state,&shot));++reads;CHECK(capture()==0);')
        harness=harness.replace('int main(void) {',r'''
static int empty_time_rows(void) {
    CHECK(begin(0,12345,6789)==0);
    for(unsigned i=0;i<5;++i)results.records.timeRecord[i]=MAX_TIMER;
    CHECK(fzx_records_begin(&state,&results));
    CHECK(table_and_terminal(0)==0);
    return 0;
}
int main(void) {
    CHECK(empty_time_rows()==0);
''')
        harness=harness.replace('           steps,reads,digest);',
            '           steps,reads,digest);printf("draw corpus %u pictures peakUI=%u peakCLUT=%u\\n",pictures,peak_ui,peak_clut);')
        with tempfile.TemporaryDirectory(prefix='fzx-records-draw-') as td:
            temp=Path(td)
            (temp/'records_source_ids.h').write_bytes(records.ids_header())
            (temp/'results_source_ids.h').write_bytes(results.ids_header())
            (temp/'records.c').write_bytes(records.closure(records.SOURCE.read_text()))
            (temp/'results.c').write_bytes(results.closure(results.SOURCE.read_text()))
            (temp/'draw.c').write_bytes(draw.closure())
            (temp/'driver.c').write_text(harness)
            outputs=[]
            for cc in ('gcc','clang'):
                exe=temp/cc
                run=subprocess.run([cc,'-std=c11','-O2','-fsigned-char','-ffp-contract=off',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(ROOT/'include'),'-I'+str(temp),
                    *(str(temp/(p+'.c')) for p in ('records','results','draw','driver')),
                    str(ROOT/'src/fzx_records_draw.c'),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stderr)
                got=subprocess.run([str(exe)],capture_output=True,text=True)
                self.assertEqual(got.returncode,0,got.stderr);outputs.append(got.stdout)
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0].strip())
            source=draw.closure().decode()
            minus=next(i for i,spec in enumerate(assets['specs']) if spec[0]=='aFont4MinusTex')
            for before,after in [('p->visible=1;', 'p->visible=0;'),
                ('{%d,8,8}'%minus,'{%d,16,8}'%minus),
                ('p->slot=(uint8_t)(slot+1);','p->slot=(uint8_t)slot;'),
                ('*out=m->snapshot;return true;',
                 '((fzx_records_snapshot_t *)s)->tick++;*out=m->snapshot;return true;')]:
                with self.subTest(control=before):
                    self.assertGreaterEqual(source.count(before),1)
                    (temp/'draw.c').write_text(source.replace(before,after))
                    command=['gcc','-std=c11','-O2','-fsigned-char','-I'+str(ROOT/'include'),'-I'+str(temp),
                        *(str(temp/(p+'.c')) for p in ('records','results','draw','driver')),
                        str(ROOT/'src/fzx_records_draw.c'),'-o',str(temp/'control')]
                    run=subprocess.run(command,capture_output=True,text=True)
                    self.assertEqual(run.returncode,0,run.stderr)
                    got=subprocess.run([str(temp/'control')],capture_output=True,text=True)
                    self.assertNotEqual(got.returncode,0,'Draw mutation survived')
                    self.assertIn('records check line',got.stderr)

if __name__=='__main__':unittest.main()
