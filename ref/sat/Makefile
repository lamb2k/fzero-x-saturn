# Reference-on-SH-2 probe image. Called by ref/sat/inner.sh inside the Yaul image; not a
# shipping build. SAT_CLOSURE is the relocatable object of the original race code.
ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
$(error YAUL_INSTALL_ROOT must point to the Yaul installation)
endif
ifeq ($(strip $(SAT_CLOSURE)),)
$(error SAT_CLOSURE must name ref_closure.o)
endif

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

SH_BUILD_DIR := $(SAT_OUT)/yaul
SH_OUTPUT_DIR := $(SAT_OUT)
SH_PROGRAM := fzx-ref-sat
SH_SRCS := main_sat.c
SH_CFLAGS += -O2 -std=gnu11 -Wno-shadow
ifeq ($(FZX_SOURCE_CAMERA),1)
SH_CFLAGS += -DFZX_SOURCE_CAMERA=1
endif
SH_LDFLAGS += $(SAT_CLOSURE)

IP_VERSION := V0.100
IP_RELEASE_DATE := 20261115
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := FZX reference on SH-2 probe
IP_MASTER_STACK_ADDR := 0x06100000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
