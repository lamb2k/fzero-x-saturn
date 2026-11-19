/* HOST only: see engine_dump.c. */
#ifndef FZX_REF_ENGINE_DUMP_H
#define FZX_REF_ENGINE_DUMP_H
#define REF_ENGINE_CONSTANTS 10
/* 0 when the course ring is not a valid closed ring of at least 3 chunks. */
unsigned ref_engine_chunk_count(void);
void ref_engine_chunk(unsigned index, float position[3]);
/* Render origin distance, far distance, group split, fov scale X/Y, frustum
 * centre X/Y, camera 0's normalised xz forward (after a camera update), and
 * the course fog start distance. */
void ref_engine_constants(float out[REF_ENGINE_CONSTANTS]);
#endif
