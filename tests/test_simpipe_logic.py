"""HOST tests of the sim-pipeline checkpoint 4 rules (src/fzx_simpipe_logic.c).

Compiles the real module with the host C compiler and checks the C-040 rules:
the independent expected-tick count (live k; replay/log bounds in replay mode
only), the batch check (cuts and short batches), the intended-submission FIFO
(a swap consumes exactly one submission, a repeat field none, a pending tag is
never overwritten on bank reuse, no tag survives a reset, ticks never go back),
and the drain/resume ownership rules (bounded wait, master only, no resume
before the first new publication)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which('gcc') or shutil.which('cc')

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "fzx_simpipe_logic.h"
static unsigned failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
static fzx_simpipe_tag_t tag(uint32_t g, uint32_t b, uint32_t s, uint32_t t) { fzx_simpipe_tag_t x = { g, b, s, t }; return x; }

/* Hook doubles for the owner. */
typedef struct { int outstanding, wait_ok, master, waits, purges; uint32_t seq; } hw_t;
static bool h_wait(void *c, uint32_t *s) { hw_t *h = c; ++h->waits; if (!h->wait_ok) return false; h->outstanding = 0; *s = h->seq; return true; }
static void h_purge(void *c) { ++((hw_t *)c)->purges; }
static bool h_master(void *c) { return ((hw_t *)c)->master; }

static void expected(void) {
    /* Live: k, whatever the log holds. */
    CHECK(fzx_simpipe_expected_ticks(3, 0, 100, 12288) == 3);
    CHECK(fzx_simpipe_expected_ticks(0, 0, 100, 12288) == 0);
    CHECK(fzx_simpipe_expected_ticks(5, 0, 12288, 12288) == 5);   /* saturated log still ticks live */
    /* Replay: bounded by the replay count and the log size. */
    CHECK(fzx_simpipe_expected_ticks(3, 7059, 7000, 12288) == 3);
    CHECK(fzx_simpipe_expected_ticks(3, 7059, 7057, 12288) == 2);
    CHECK(fzx_simpipe_expected_ticks(3, 7059, 7059, 12288) == 0);
    CHECK(fzx_simpipe_expected_ticks(3, 7059, 7100, 12288) == 0);  /* past the end: frozen */
    CHECK(fzx_simpipe_expected_ticks(4, 20000, 12286, 12288) == 2); /* log bound below replay */
}

static void batch(void) {
    fzx_simpipe_batch_t b;
    b.executed = 3; b.cut = 0; CHECK(fzx_simpipe_batch_check(3, &b) == 0);
    b.executed = 4; b.cut = 0; CHECK(fzx_simpipe_batch_check(3, &b) == 1);
    b.executed = 2; b.cut = 0; CHECK(fzx_simpipe_batch_check(3, &b) == 2);  /* short, no cut */
    b.executed = 2; b.cut = 1; CHECK(fzx_simpipe_batch_check(3, &b) == 0);  /* cut at tick 2 */
    b.executed = 0; b.cut = 1; CHECK(fzx_simpipe_batch_check(3, &b) == 0);  /* cut at the first */
    b.executed = 3; b.cut = 1; CHECK(fzx_simpipe_batch_check(3, &b) == 3);  /* cut at the end */
    b.executed = 7; b.cut = 0; CHECK(fzx_simpipe_batch_check(7, &b) == 4);  /* beyond six ticks */
}

static void fifo(void) {
    fzx_simpipe_fifo_t f;
    fzx_simpipe_tag_t a = tag(1, 0, 1, 10), b = tag(1, 1, 2, 12), c = tag(1, 0, 3, 15), x;
    memset(&f, 0, sizeof(f));   /* reset keeps the statistics across generations */
    fzx_simpipe_fifo_reset(&f, 1);
    CHECK(fzx_simpipe_fifo_submit(&f, &a) == FZX_SIMPIPE_FIFO_OK);
    CHECK(fzx_simpipe_fifo_submit(&f, &b) == FZX_SIMPIPE_FIFO_OK);
    /* Same-bank reuse while bank 0's tag is still pending: refused. */
    CHECK(fzx_simpipe_fifo_submit(&f, &c) == FZX_SIMPIPE_FIFO_BANK_PENDING);
    CHECK(f.count == 2);
    /* A repeat field consumes nothing. */
    CHECK(fzx_simpipe_fifo_observe(&f, false, NULL) == FZX_SIMPIPE_FIFO_OK);
    CHECK(f.count == 2 && f.repeats == 1 && f.swaps == 0);
    /* A swap consumes exactly the oldest. */
    CHECK(fzx_simpipe_fifo_observe(&f, true, &a) == FZX_SIMPIPE_FIFO_OK);
    CHECK(f.count == 1 && f.swaps == 1 && f.presented.tick == 10);
    /* Now bank 0 is free again. */
    CHECK(fzx_simpipe_fifo_submit(&f, &c) == FZX_SIMPIPE_FIFO_OK);
    /* A stale tag on the swapped bank (the old one, same bank) is a mismatch. */
    x = tag(1, 1, 0, 9);
    CHECK(fzx_simpipe_fifo_observe(&f, true, &x) == FZX_SIMPIPE_FIFO_MISMATCH);
    CHECK(f.count == 2);   /* nothing consumed on a mismatch */
    CHECK(fzx_simpipe_fifo_observe(&f, true, &b) == FZX_SIMPIPE_FIFO_OK);
    CHECK(fzx_simpipe_fifo_observe(&f, true, &c) == FZX_SIMPIPE_FIFO_OK);
    CHECK(f.count == 0 && f.swaps == 3);
    /* A swap with nothing pending. */
    CHECK(fzx_simpipe_fifo_observe(&f, true, &c) == FZX_SIMPIPE_FIFO_EMPTY);
    /* Sequence must be the next one. */
    x = tag(1, 1, 9, 20);
    CHECK(fzx_simpipe_fifo_submit(&f, &x) == FZX_SIMPIPE_FIFO_SEQUENCE);
    /* Another generation's tag. */
    x = tag(2, 1, 4, 20);
    CHECK(fzx_simpipe_fifo_submit(&f, &x) == FZX_SIMPIPE_FIFO_GENERATION);
    /* Ticks never move backwards within a generation. */
    x = tag(1, 1, 4, 14);
    CHECK(fzx_simpipe_fifo_submit(&f, &x) == FZX_SIMPIPE_FIFO_OK);
    CHECK(fzx_simpipe_fifo_observe(&f, true, &x) == FZX_SIMPIPE_FIFO_TICK_ORDER);
    /* A reset drops every pending tag: nothing carries across it. */
    fzx_simpipe_fifo_reset(&f, 2);
    CHECK(f.count == 0 && !f.presented_valid);
    x = tag(1, 1, 4, 14);
    CHECK(fzx_simpipe_fifo_observe(&f, true, &x) == FZX_SIMPIPE_FIFO_EMPTY);
    x = tag(2, 0, 1, 0);
    CHECK(fzx_simpipe_fifo_submit(&f, &x) == FZX_SIMPIPE_FIFO_OK);
    CHECK(fzx_simpipe_fifo_observe(&f, true, &x) == FZX_SIMPIPE_FIFO_OK);
    /* Full: four pending, alternate banks would collide first; use distinct
     * banks 0..3 to reach the FIFO bound. */
    fzx_simpipe_fifo_reset(&f, 3);
    for (uint32_t i = 0; i < FZX_SIMPIPE_FIFO; ++i) { x = tag(3, i, i + 1, i); CHECK(fzx_simpipe_fifo_submit(&f, &x) == FZX_SIMPIPE_FIFO_OK); }
    x = tag(3, 9, FZX_SIMPIPE_FIFO + 1, 9);
    CHECK(fzx_simpipe_fifo_submit(&f, &x) == FZX_SIMPIPE_FIFO_FULL);
}

static void owner(void) {
    fzx_simpipe_owner_t o; hw_t hw = { 0, 1, 1, 0, 0, 0 };
    fzx_simpipe_owner_hooks_t h = { h_wait, h_purge, h_master, &hw };
    fzx_simpipe_owner_init(&o, 1);
    CHECK(o.state == FZX_SIMPIPE_RUNNING);
    CHECK(fzx_simpipe_owner_dispatch(&o) == FZX_SIMPIPE_OWNER_OK);
    hw.outstanding = 1; hw.seq = 1;
    /* Drain waits for the outstanding job before handing over, and purges. */
    CHECK(fzx_simpipe_owner_drain(&o, &h, 2) == FZX_SIMPIPE_OWNER_OK);
    CHECK(hw.waits == 1 && hw.purges == 1 && !hw.outstanding);
    CHECK(o.state == FZX_SIMPIPE_DRAINED && o.generation == 2 && o.completed_sequence == 1);
    /* Idempotent. */
    CHECK(fzx_simpipe_owner_drain(&o, &h, 2) == FZX_SIMPIPE_OWNER_OK);
    CHECK(hw.waits == 1);
    /* No dispatch while drained. */
    CHECK(fzx_simpipe_owner_dispatch(&o) == FZX_SIMPIPE_OWNER_STATE);
    /* No resume before the new state's first publication. */
    CHECK(fzx_simpipe_owner_resume(&o, &h, false) == FZX_SIMPIPE_OWNER_NOT_READY);
    CHECK(fzx_simpipe_owner_resume(&o, &h, true) == FZX_SIMPIPE_OWNER_OK);
    CHECK(o.state == FZX_SIMPIPE_RUNNING);
    /* Resume without a drain. */
    CHECK(fzx_simpipe_owner_resume(&o, &h, true) == FZX_SIMPIPE_OWNER_STATE);
    /* Drain or resume off the master (the slave never waits on itself). */
    hw.master = 0;
    CHECK(fzx_simpipe_owner_drain(&o, &h, 3) == FZX_SIMPIPE_OWNER_NOT_MASTER);
    hw.master = 1;
    /* Bounded wait expiry. */
    CHECK(fzx_simpipe_owner_dispatch(&o) == FZX_SIMPIPE_OWNER_OK);
    hw.outstanding = 1; hw.wait_ok = 0;
    CHECK(fzx_simpipe_owner_drain(&o, &h, 3) == FZX_SIMPIPE_OWNER_TIMEOUT);
    CHECK(o.state != FZX_SIMPIPE_DRAINED);
}

int main(void) {
    expected(); batch(); fifo(); owner();
    printf(failures ? "FAILED %u\n" : "OK\n", failures);
    return failures != 0;
}
'''


@unittest.skipUnless(CC, 'no host C compiler')
class SimpipeLogic(unittest.TestCase):
    def test_rules(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / 'driver.c'
            exe = Path(d) / 'driver'
            src.write_text(DRIVER)
            subprocess.run([CC, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
                            str(src), str(ROOT / 'src/fzx_simpipe_logic.c'), '-o', str(exe)], check=True)
            r = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout)
            self.assertIn('OK', r.stdout)


if __name__ == '__main__':
    unittest.main()
