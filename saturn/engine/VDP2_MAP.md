# E0 VDP2 VRAM and CRAM map

VDP2 VRAM: 512 KB in four 128 KB banks (RAMCTL 0x031b: A0 character patterns, A1 pattern names, B0 coefficients, B1 unused by RBG0).

| Bank | Start | End | Bytes | Allocation | Source |
| --- | --- | --- | --- | --- | --- |
| A0 | 0x00000 | 0x01000 | 4096 | RBG0 character patterns A (floor) | cells 0..124 referenced by map A; FLOORCHR.BIN |
| A0 | 0x01000 | 0x02000 | 4096 | RBG0 character patterns B (clouds) | cells 128..252 referenced by map B; clouds_vdp2_native.c CLOUD_CHARS |
| A1 | 0x20000 | 0x20060 | 96 | RBG0 rotation parameter table A (floor) | RPTA; floor_rbg_native.c FLOOR_RP |
| A1 | 0x20080 | 0x200e0 | 96 | RBG0 rotation parameter table B (clouds) | RPTA+0x80; clouds_vdp2_native.c CLOUD_RP |
| A1 | 0x24000 | 0x28000 | 16384 | RBG0 map A (floor) | MPOFR/MPABRA; FLOOR_MAP |
| A1 | 0x28000 | 0x2c000 | 16384 | RBG0 map B (clouds) | MPOFR/MPABRB; CLOUD_MAP |
| A1 | 0x2c000 | 0x2c380 | 896 | Line window W1 (clouds) | LWTA1; CLOUD_WINDOW |
| A1 | 0x2c400 | 0x2c5c0 | 448 | Back screen colour (one per line) | BKTA; clouds_vdp2_native.c SKY_BACK |
| B0 | 0x40000 | 0x60000 | 131072 | RBG0 coefficient table (A and B share it) | KTAOF; FLOORKOF.BIN at B0 (FLOOR_COEFF) |
| B1 | 0x60000 | 0x60380 | 896 | Line window W0 (floor span; selects parameter A) | LWTA0; FLOOR_WINDOW |
| B1 | 0x7c000 | 0x7dfe0 | 8160 | NBG3 character patterns (dbgio font, 256 glyphs, 16 colours) | Yaul dbgio; written glyphs to +0x1fe0 |
| B1 | 0x7e000 | 0x80000 | 8192 | NBG3 map (dbgio text, 64x64 cells) | MPOFN/MPABN3/MPCDN3 (Yaul dbgio) |

Overlaps: none
Note: NBG3 cells outside the displayed 40x28 area reference cells 12288, 14401 (VRAM 0x60000, 0x70820); they are never displayed (NBG3 does not scroll).
Note: Map cell (63,63) at 0x7fffe holds 0x9841: the boot back-screen colour written by user_init (vdp2_scrn_back_color_set at VDP2_VRAM_ADDR(3,0x1fffe)) before the clouds move the back screen to A1 0x2c400. That cell is never displayed.
Written bytes outside every allocation: 0
- A0: 8192 of 131072 bytes allocated, 122880 free
- A1: 34304 of 131072 bytes allocated, 96768 free
- B0: 131072 of 131072 bytes allocated, 0 free
- B1: 17248 of 131072 bytes allocated, 113824 free

CRAM: mode 0 (1024 RGB1555 entries; CRAOFA 0x0000, CRAOFB 0x0000). Written entries: 1..65, 1009..1015, 1017..1023.

CRAM owners:

| Entries | Owner | Source |
| --- | --- | --- |
| 0 | unused (transparent code 0) | |
| 1 | floor fog colour (RBG0 band 7 polygons on VDP1 read it) | floor_rbg_native.c, VDP2_CRAM_MODE_1_OFFSET(0,0,1) |
| 2..65 | sky line colours and cloud band colours (64) | clouds_vdp2_native.c, BAND_CRAM_FIRST 2, BAND_MAX 64 |
| 1008..1023 | dbgio text palette (palette 63; entry 1016 is zero) | Yaul dbgio, NBG3 palette from PNCN3 |
| 66..1007 | free | |

How this map was made: an `E0_VDP2_MAP=1` build of the default image (BIN 2758f9881ed90ab9, same layout code as default BIN 64f671b6199cdec3) copies the VDP2 register shadow and CRAM after setup. `tools/e0_vdp2map.py BUILD RUN` decodes them, finds each table's extent from the registers and the map contents, checks for overlaps, and checks the final VDP2 VRAM dump of the 3-lap run for written bytes outside every allocation (Ymir, emulator).
