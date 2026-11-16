FZX_MODEL_ART ?= fixture
FZX_MODEL_DIVU ?= 0
SH_PROGRAM := fzx-model-probe
SH_BUILD_DIR := build-$(FZX_MODEL_ART)-$(FZX_MODEL_DIVU)/yaul
SH_OUTPUT_DIR := build-$(FZX_MODEL_ART)-$(FZX_MODEL_DIVU)
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
ifeq ($(FZX_MODEL_ART),fixture)
MODEL_DIR := ../../generated/model_fixture
SH_CFLAGS += -DFZX_EXPECT_MODEL_FIXTURE=1
else ifeq ($(FZX_MODEL_ART),retail)
MODEL_DIR := ../../generated/source_art
SH_CFLAGS += -DFZX_EXPECT_MODEL_FIXTURE=0
else
$(error FZX_MODEL_ART must be fixture or retail)
endif
MODEL_RECEIPT := $(shell sh ../check_receipt.sh $(MODEL_DIR)/source_art.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(MODEL_RECEIPT)),)
$(error Model input refused: $(MODEL_RECEIPT))
endif
SH_SRCS := main.c ../../src/fzx_material.c ../../src/fzx_model.c
SH_CFLAGS += -O2 -std=c11 -Wall -Wextra -Werror -I../../include -I$(MODEL_DIR) -DFZX_MODEL_DIVU=$(FZX_MODEL_DIVU)
IP_VERSION := V0.100
IP_RELEASE_DATE := 20261115
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := Native machine material probe
IP_MASTER_STACK_ADDR := 0x06100000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

.PHONY: symbols
symbols:
	$(YAUL_ARCH_SH_PREFIX)-nm $(SH_BUILD_DIR)/$(SH_PROGRAM).elf > $(SH_OUTPUT_DIR)/$(SH_PROGRAM).sym
