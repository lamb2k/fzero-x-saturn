#ifndef FZX_REF_SCENE_PROBE_H
#define FZX_REF_SCENE_PROBE_H
#define REF_SCENE_PROBE_CASES 256
int ref_scene_probe_init(void);
int ref_scene_probe_step(unsigned int frame, unsigned int hashes[2]);
#endif
