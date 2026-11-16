from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from road_environment import course_fog

class EnvironmentTests(unittest.TestCase):
    def test_source_wiring_and_controls(self):
        fields=['texture','courseFogR','courseFogG','courseFogB','racerFogR','racerFogG','racerFogB',
                'backgroundSpriteR','backgroundSpriteG','backgroundSpriteB','flags']
        header='typedef struct CourseSkyboxes {'+''.join('int '+f+';' for f in fields)+'} CourseSkyboxes;'
        source='CourseSkyboxes* sCourseSkyboxes[] = {'+','.join('&sky%d'%i for i in range(8))+'};\n'
        for i in range(8):source+='CourseSkyboxes sky%d = {texture,%d,%d,%d,0,0,0,0,0,0,FLAGS};\n'%(i,i+11,i+21,i+31)
        source+='sBackgroundCtx.skybox = sCourseSkyboxes[gSkyboxType];\n'
        for i,c in enumerate('RGB'):source+='courseInfo->courseFogColors[%d] = sBackgroundCtx.skybox->courseFog%s;\n'%(i,c)
        for i in range(8):self.assertEqual(course_fog(source,header,i)['course_fog'],[i+11,i+21,i+31])
        for old,new in [('&sky7','&sky6'),('courseFogR;','courseFogG;'),('skybox = sCourseSkyboxes','skybox = otherTable')]:
            with self.assertRaises(ValueError):course_fog(source.replace(old,new),header,0)
        with self.assertRaises(ValueError):course_fog(source,header.replace('courseFogR','wrong'),0)
        with self.assertRaises(ValueError):course_fog(source,header,8)

if __name__=='__main__':unittest.main()
