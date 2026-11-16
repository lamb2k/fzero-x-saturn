# FZX_HUD_ART=fixture (synthetic) or retail (verified local source-art export).
FZX_HUD_ART ?= fixture
SH_PROGRAM := fzx-hud-probe
SH_BUILD_DIR := build-$(FZX_HUD_ART)/yaul
SH_OUTPUT_DIR := build-$(FZX_HUD_ART)
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
ifeq ($(FZX_HUD_ART),fixture)
HUD_DIR := ../../generated/hud_fixture
SH_CFLAGS += -DFZX_EXPECT_HUD_FIXTURE=1
else ifeq ($(FZX_HUD_ART),retail)
HUD_DIR := ../../generated/source_art
SH_CFLAGS += -DFZX_EXPECT_HUD_FIXTURE=0
else
$(error FZX_HUD_ART must be fixture or retail)
endif
HUD_RECEIPT := $(shell sh ../check_receipt.sh $(HUD_DIR)/source_art.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(HUD_RECEIPT)),)
$(error HUD input refused: $(HUD_RECEIPT))
endif
SH_SRCS := main.c ../../src/fzx_hud.c ../../saturn/hud_native.c
SH_CFLAGS += -O2 -std=c11 -Wall -Wextra -Werror -I../../include -I../../saturn -I$(HUD_DIR)
IP_VERSION := V0.100
IP_RELEASE_DATE := 20261115
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := Native HUD asset probe
IP_MASTER_STACK_ADDR := 0x06100000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
