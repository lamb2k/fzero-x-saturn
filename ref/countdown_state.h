#ifndef FZX_COUNTDOWN_STATE_H
#define FZX_COUNTDOWN_STATE_H
/* Read-only stock single-player countdown presentation. The matrix is the
 * packed source Mtx reconstructed as row-major signed Q16.16, not a new pose. */
typedef struct {
    int timer;
    int matrix[16];
    unsigned int faults;
} RefCountdown;
int ref_countdown(RefCountdown *out);
#endif
