"""Pinned-source extraction fails closed; it never emits native Gfx calls."""
from pathlib import Path
import sys, unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import road_plan

class RoadPlanTests(unittest.TestCase):
    def test_stock_guards(self):
        self.assertEqual(road_plan.stock('a\n#ifdef PORT\nb\n#ifdef EXPANSION_KIT\nc\n#endif\n#else\nd\n#endif\ne\n'),'a\nd\ne\n')
        for text in ('#ifdef UNKNOWN\n#endif\n','#endif\n','#ifdef PORT\n'):
            with self.assertRaises(ValueError):road_plan.stock(text)
    def test_source_boundary(self):
        p=ROOT/'upstream/G-Diffuser/decomp/src/game/course.c'
        if not p.is_file():self.skipTest('source submodule not initialized')
        text=p.read_text();result=road_plan.native(text)
        self.assertIn('ref_road_group(out, furthestChunkGroup, 1);',result)
        self.assertIn('ref_road_group(out, furthestChunkGroup, -1);',result)
        self.assertNotIn('gSP',result);self.assertNotIn('gDP',result)
        for old,new in [('sCourseFarRenderDistance < chunk->depth','sCourseFarRenderDistance <= chunk->depth'),
                        ('var_fv1 > 30.0f','var_fv1 > 31.0f')]:
            changed=text.replace(old,new);self.assertNotEqual(changed,text)
            with self.assertRaisesRegex(ValueError,'Source course changed'):road_plan.native(changed)

if __name__=='__main__':unittest.main()
