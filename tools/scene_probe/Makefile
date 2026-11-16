SH_PROGRAM := fzx-scene-probe
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
FZX_SCENE_CONTROL ?= 0
SH_SRCS := main.c
SH_CFLAGS += -O2 -std=c11 -I../../ref -Ibuild -DFZX_SCENE_CONTROL=$(FZX_SCENE_CONTROL)
SH_LDFLAGS += build/scene.o
FZX_SCENE_RECEIPT := $(shell bash ../check_receipt.sh build/expected.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_SCENE_RECEIPT)),)
$(error Scene probe inputs stale: $(FZX_SCENE_RECEIPT))
endif
IP_VERSION := V0.100
IP_RELEASE_DATE := 20261116
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := Source scene state probe
IP_MASTER_STACK_ADDR := 0x06100000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
