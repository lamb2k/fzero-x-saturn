# SATURN_DIR=tools/dual_rbg_probe tools/saturn_build.sh PROBE_MODE=0
PROBE_MODE ?= 0
SH_PROGRAM := dual-rbg-$(PROBE_MODE)
SH_BUILD_DIR := build-$(PROBE_MODE)
IMAGE_DIRECTORY := cd-$(PROBE_MODE)
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
SH_SRCS := probe.c
SH_CFLAGS += -O2 -std=c11 -Wall -Wextra -DPROBE_MODE=$(PROBE_MODE)
IP_VERSION := V0.100
IP_RELEASE_DATE := 20261116
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := Synthetic dual rotation probe
IP_MASTER_STACK_ADDR := 0x06100000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
