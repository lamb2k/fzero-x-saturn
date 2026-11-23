CC ?= cc
PYTHON ?= python3
BUILD ?= build/host
SH2ZAM_ROOT ?= third_party/SH2ZAM
CFLAGS ?= -O2 -g
WARNINGS := -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wpedantic
CPPFLAGS += -Iinclude -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_FORCE_PORTABLE=1
CORE := src/fzx.c $(wildcard $(SH2ZAM_ROOT)/saturn/src/*.c)
HEADERS := include/fzx.h src/demo_course.h

.PHONY: all test sanitize web saturn clean
all: test
$(BUILD):
	mkdir -p $@
$(BUILD)/test-core: tests/test_core.c $(CORE) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) tests/test_core.c $(CORE) -lm $(LDFLAGS) -o $@
# The SH-2 float library, checked bit for bit against the host's SSE binary32 (x86 hosts).
$(BUILD)/test-float: tests/test_fzx_float.c src/sh2float/fzx_float.c src/sh2float/fzx_float.h | $(BUILD)
	$(CC) -Isrc/sh2float $(CFLAGS) -std=c11 -Wall -Wextra -Werror -msse2 -fno-fast-math -ffp-contract=off 		tests/test_fzx_float.c src/sh2float/fzx_float.c $(LDFLAGS) -o $@
$(BUILD)/test-model: tests/test_model.c src/fzx_model.c src/fzx_material.c include/fzx_model.h include/fzx_material.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) tests/test_model.c src/fzx_model.c src/fzx_material.c $(LDFLAGS) -o $@
$(BUILD)/test-iso-root: tests/test_iso_root.c saturn/iso_root.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARNINGS) tests/test_iso_root.c $(LDFLAGS) -o $@
test: $(BUILD)/test-core $(BUILD)/test-float $(BUILD)/test-model $(BUILD)/test-iso-root
	$(BUILD)/test-core
	$(BUILD)/test-float 2 65537
	$(BUILD)/test-model
	$(BUILD)/test-iso-root
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
sanitize:
	$(MAKE) test BUILD=build/sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'
web:
	bash tools/build_web.sh
saturn:
	$(MAKE) -C saturn
clean:
	rm -rf build/host build/sanitize
