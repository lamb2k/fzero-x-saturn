"""Read the source Background_Init course-fog binding without running the scene."""
import re

def course_fog(source,header,skybox):
    if not 0<=skybox<8:raise ValueError('Invalid source skybox index')
    match=re.search(r'typedef struct CourseSkyboxes\s*\{(.*?)\}\s*CourseSkyboxes;',header,re.S)
    if not match:raise ValueError('Missing source skybox layout')
    fields=re.findall(r'\b(\w+)\s*;',match[1])
    expected=['texture','courseFogR','courseFogG','courseFogB','racerFogR','racerFogG','racerFogB',
              'backgroundSpriteR','backgroundSpriteG','backgroundSpriteB','flags']
    if fields!=expected:raise ValueError('Source skybox layout changed')
    table=re.search(r'CourseSkyboxes\* sCourseSkyboxes\[\]\s*=\s*\{(.*?)\};',source,re.S)
    if not table:raise ValueError('Missing source skybox table')
    names=re.findall(r'&([A-Za-z_]\w*)',table[1])
    if len(names)!=8 or len(set(names))!=8:raise ValueError('Source skybox table changed')
    name=names[skybox]
    init=re.search(r'CourseSkyboxes\s+'+re.escape(name)+r'\s*=\s*\{(.*?)\};',source,re.S)
    if not init:raise ValueError('Missing source skybox initializer')
    values=[x.strip() for x in init[1].split(',')]
    if len(values)!=len(fields):raise ValueError('Source skybox initializer changed')
    rgb=[int(values[i],0) for i in range(1,4)]
    if any(x<0 or x>255 for x in rgb):raise ValueError('Invalid source fog color')
    # Require the actual initialization wiring as well as the table's layout.
    if 'sBackgroundCtx.skybox = sCourseSkyboxes[gSkyboxType];' not in source:
        raise ValueError('Source skybox selection changed')
    for i,channel in enumerate('RGB'):
        statement='courseInfo->courseFogColors[%d] = sBackgroundCtx.skybox->courseFog%s;'%(i,channel)
        if statement not in source:raise ValueError('Source fog initialization changed')
    return dict(skybox=skybox,symbol=name,course_fog=rgb)
