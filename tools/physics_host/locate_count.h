/* Included first (-include) by tools/physics_host/build.sh with LOCATE_COUNT=1: the hook of SH2ZAM
 * vehicle-dynamics-6 counts each closest-point search that the stop ended. */
#ifndef FZX_LOCATE_COUNT_H
#define FZX_LOCATE_COUNT_H
extern unsigned fzx_phys_locate_stops;
#define SH2Z_VD_LOCATE_FAR_HIT() (fzx_phys_locate_stops++)
#endif
