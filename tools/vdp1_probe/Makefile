FZX_CHART_ART ?= fixture
SH_PROGRAM := fzx-chart-probe
SH_BUILD_DIR := build-$(FZX_CHART_ART)/yaul
SH_OUTPUT_DIR := build-$(FZX_CHART_ART)
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
ifeq ($(FZX_CHART_ART),fixture)
SH_CFLAGS += -DFZX_EXPECT_CHART_FIXTURE=1
else ifeq ($(FZX_CHART_ART),retail)
SH_CFLAGS += -DFZX_EXPECT_CHART_FIXTURE=0
else
$(error FZX_CHART_ART must be fixture or retail)
endif
CHART_DIR := ../../generated/charts_$(FZX_CHART_ART)
CHART_RECEIPT := $(shell sh ../check_receipt.sh $(CHART_DIR)/charts.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(CHART_RECEIPT)),)
$(error Chart input refused: $(CHART_RECEIPT))
endif
SH_SRCS := main.c
SH_CFLAGS += -O2 -std=c11 -Wall -Wextra -Werror -I$(CHART_DIR)
IP_VERSION := V0.100
IP_RELEASE_DATE := 20261116
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := Native VDP1 chart probe
IP_MASTER_STACK_ADDR := 0x06100000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

.PHONY: symbols
symbols:
	$(YAUL_ARCH_SH_PREFIX)-nm $(SH_BUILD_DIR)/$(SH_PROGRAM).elf > $(SH_OUTPUT_DIR)/$(SH_PROGRAM).sym
