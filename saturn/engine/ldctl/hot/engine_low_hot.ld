/* ENG_LOW_HOT_CONTROL=1 (census control only): a function every race frame runs, linked into .low, so the
 * census check (no low work RAM PC in phases 100 and 204) must fail. */
*(.text.fzx_race_frame)
