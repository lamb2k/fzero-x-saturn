FZX_PALETTE_ART ?= fixture
SH_PROGRAM := fzx-palette-probe
SH_BUILD_DIR := build-$(FZX_PALETTE_ART)/yaul
SH_OUTPUT_DIR := build-$(FZX_PALETTE_ART)
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
ifeq ($(FZX_PALETTE_ART),fixture)
SH_CFLAGS += -DFZX_EXPECT_PALETTE_FIXTURE=1
else ifeq ($(FZX_PALETTE_ART),retail)
SH_CFLAGS += -DFZX_EXPECT_PALETTE_FIXTURE=0
else
$(error FZX_PALETTE_ART must be fixture or retail)
endif
PALETTE_DIR := ../../generated/palette_$(FZX_PALETTE_ART)
PALETTE_RECEIPT := $(shell sh ../check_receipt.sh $(PALETTE_DIR)/machine.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(PALETTE_RECEIPT)),)
$(error Chart input refused: $(PALETTE_RECEIPT))
endif
SH_SRCS := main.c ../../src/fzx_machine.c
SH_CFLAGS += -O2 -std=c11 -Wall -Wextra -Werror -I$(PALETTE_DIR) -I../../include
IP_VERSION := V0.100
IP_RELEASE_DATE := 20261116
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := Native moving machine probe
IP_MASTER_STACK_ADDR := 0x06100000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

.PHONY: symbols
symbols:
	$(YAUL_ARCH_SH_PREFIX)-nm $(SH_BUILD_DIR)/$(SH_PROGRAM).elf > $(SH_OUTPUT_DIR)/$(SH_PROGRAM).sym
