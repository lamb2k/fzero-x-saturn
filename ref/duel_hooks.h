/* Duel reference hooks (attacks lane, C-092): force-included into the duel build's private copy of
 * racer.c (ref/duel_build.sh) and implemented in ref/duel_probe.c. They record what the original
 * computed (pair touch, closing comparison, push, damage calls, KO credit); the closing hook returns
 * the original's own comparison, except on the one tick a branch replay forces. HOST reference role
 * only. MIT; see ../LICENSE. */
#ifndef FZX_REF_DUEL_HOOKS_H
#define FZX_REF_DUEL_HOOKS_H

struct Racer;
extern int ref_duel_ctx;   /* 1 while the first-contact branch hands out damage */
void ref_duel_ko(struct Racer* victim, struct Racer* attacker, int window);
void ref_duel_damage(struct Racer* racer, float amount);
void ref_duel_touch(struct Racer* racer, struct Racer* racer2, int was_colliding, float d, float impact, float share_racer,
                    float share_racer2);
int ref_duel_closing(struct Racer* racer, struct Racer* racer2, float dx, float dy, float dz, float dvx, float dvy, float dvz,
                     float closing);
void ref_duel_push(struct Racer* racer, struct Racer* racer2, float k_racer, float k_racer2, float px, float py, float pz);

#endif
