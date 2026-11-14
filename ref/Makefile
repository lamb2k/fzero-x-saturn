# Float reference harness (card G1.2). Builds the decomp's one-racer closure natively.
# Needs the decomp submodule: git submodule update --init upstream/G-Diffuser, then its decomp.
#   make -C ref                    # gcc -O2
#   make -C ref CC=clang OPT=-O0   # the four-way determinism check varies these
# PORT_RACER=1 compiles racer.c alone with PORT defined (C11; port hooks become no-op
# stubs): the G1.1 control. A full PORT build needs EXPANSION_KIT and G-Diffuser's runtime.
ifeq ($(origin CC),default)
CC := gcc
endif
OPT ?= -O2
PORT ?= 0
PORT_RACER ?= 0
ROOT := $(abspath ..)
DECOMP := $(ROOT)/upstream/G-Diffuser/decomp
GDX_INCLUDE := $(ROOT)/upstream/G-Diffuser/include
BUILD ?= $(ROOT)/build/ref/$(notdir $(CC))$(OPT)$(if $(filter 1,$(PORT)),-port)$(if $(filter 1,$(PORT_RACER)),-portracer)

ifeq ($(wildcard $(DECOMP)/src/game/racer.c),)
$(error decomp submodule missing at $(DECOMP))
endif

# binary32 discipline: no contraction, no fast-math, SSE scalar float on 32-bit x86.
FLOAT := -ffp-contract=off -fno-fast-math -msse2 -mfpmath=sse -fexcess-precision=standard
# sqrtf is pinned to ref_sqrtf (ref_platform.c, SSE sqrtss). glibc's i386 sqrtf returns an
# unrounded x87 result, which Clang -O2 kept in double and GCC rounded: an ulp apart in
# func_8009DB28's spline tensions. The importer's independent version sides with rounding.
DEFS := -Dsqrtf=ref_sqrtf -D_LANGUAGE_C -DNON_MATCHING -DAVOID_UB -DVERSION_US=1 -DBUILD_VERSION=VERSION_I \
        -DASSET_VERSION=us -DASSET_REVISION=rev0 -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 \
        -D_MIPS_SZPTR=32 $(if $(filter 1,$(PORT)),-DPORT)
INCS := -I$(DECOMP)/include -I$(DECOMP) -I$(DECOMP)/include/libultra -I$(DECOMP)/include/PR \
        -I$(GDX_INCLUDE) -I.
CSTD := $(if $(filter 1,$(PORT)),-std=gnu11,-std=gnu90)
CFLAGS := -m32 $(CSTD) $(OPT) -g $(FLOAT) -fno-strict-aliasing -fwrapv -fno-builtin \
          -funsigned-char -w $(DEFS) $(INCS)
ifeq ($(notdir $(CC)),gcc)
CFLAGS := $(filter-out -fexcess-precision=standard,$(CFLAGS)) -fexcess-precision=standard
else
# Clang 16+ makes these errors even under -w; the decomp relies on them (N64 pointers are
# 32-bit integers). They do not change generated float code.
CFLAGS := $(filter-out -fexcess-precision=standard,$(CFLAGS)) -ffp-eval-method=source -Wno-error=int-conversion \
          -Wno-error=incompatible-function-pointer-types -Wno-error=implicit-function-declaration \
          -Wno-error=implicit-int -Wno-error=return-type -Wno-error=incompatible-pointer-types
endif

DECOMP_SRCS := game/racer game/course game/course_gadgets game/camera sys/math game/EA90 \
               game/EA20 game/common
DECOMP_OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(notdir $(DECOMP_SRCS))))
HARNESS_OBJS := $(BUILD)/ref_platform.o $(BUILD)/ref_game.o $(BUILD)/ref_main.o

.PHONY: all clean
all: $(BUILD)/ref

$(BUILD):
	mkdir -p $@

define decomp_rule
$(BUILD)/$(notdir $(1)).o: $(DECOMP)/src/$(1).c | $(BUILD)
	$(CC) $(CFLAGS) -c $$< -o $$@
endef
$(foreach s,$(filter-out game/racer,$(DECOMP_SRCS)),$(eval $(call decomp_rule,$(s))))

# racer.c reads N64 hardware directly: ROM_READ (38 machine-tuning words from the ROM's
# first 4 KB, all in Racer_InitMachineStats) and IO_READ/IO_WRITE (PI status polling).
# A mechanical rewrite routes them to ref_rom_read32 / ref_io_read / ref_io_write;
# nothing else in the file changes.
$(BUILD)/racer.c: $(DECOMP)/src/game/racer.c | $(BUILD)
	sed -e 's/\bROM_READ(/REF_ROM_READ(/g' -e 's/\bIO_READ(/REF_IO_READ(/g' \
	    -e 's/\bIO_WRITE(/REF_IO_WRITE(/g' $< > $@
$(BUILD)/racer.o: $(BUILD)/racer.c ref_hw.h | $(BUILD)
	$(CC) $(CFLAGS) $(if $(filter 1,$(PORT_RACER)),-DPORT -std=gnu11 -I$(ROOT)/upstream/G-Diffuser/port) \
	    -include ref_hw.h -I$(DECOMP)/src/game -c $< -o $@

$(BUILD)/ref_platform.o: ref_platform.c ref.h | $(BUILD)
	$(CC) $(filter-out -Dsqrtf=ref_sqrtf,$(CFLAGS)) -c $< -o $@
$(BUILD)/ref_game.o: ref_game.c ref.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
# The libc side sees system headers only, never the decomp's.
$(BUILD)/ref_main.o: ref_main.c ref.h ref_stubs.h | $(BUILD)
	$(CC) -m32 -std=gnu99 $(OPT) -g $(FLOAT) -Wall -I. -c $< -o $@

# Stubs cover whatever the closure and harness reference but nothing here defines.
# Only the game side is scanned: ref_main.o's undefined symbols are libc's, and a stub
# there would silently replace a real libc function.
GAME_OBJS := $(DECOMP_OBJS) $(BUILD)/ref_platform.o $(BUILD)/ref_game.o
$(BUILD)/stubs.c: gen_stubs.py $(GAME_OBJS)
	python3 gen_stubs.py $@ $(GAME_OBJS)
$(BUILD)/stubs.o: $(BUILD)/stubs.c ref_stubs.h
	$(CC) -m32 -w -I. -c $< -o $@

$(BUILD)/ref: $(DECOMP_OBJS) $(HARNESS_OBJS) $(BUILD)/stubs.o
	$(CC) -m32 $^ -lm -o $@

clean:
	rm -rf $(ROOT)/build/ref
