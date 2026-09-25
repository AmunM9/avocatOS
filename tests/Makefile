# Host unit tests for avocatOS core logic.
#   make -C tests           build + run every suite
#   make -C tests coverage  line coverage of avo_core
CORE    := ../components/avo_core
CFLAGS  := -std=c11 -Wall -Wextra -Werror -O0 -g -I$(CORE)/include
SRCS    := $(wildcard $(CORE)/src/*.c)
BUILD   := build
SUITES  := test_core test_phone test_phase3

.PHONY: test coverage clean
test: $(addprefix $(BUILD)/,$(SUITES))
	@for s in $(SUITES); do echo "== $$s"; ./$(BUILD)/$$s 2>/dev/null | tail -1 || exit 1; done

$(BUILD)/%: %.c $(SRCS) $(CORE)/include/avo_core.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(SRCS) -lm

coverage:
	@rm -rf $(BUILD)/cov && mkdir -p $(BUILD)/cov
	cd $(BUILD)/cov && for s in $(SUITES); do \
	  $(CC) -std=c11 -O0 -g --coverage -I../../$(CORE)/include -o $$s ../../$$s.c $(addprefix ../../,$(SRCS)) -lm && ./$$s >/dev/null 2>&1; done; \
	  xcrun llvm-cov gcov *.gcda 2>/dev/null | grep -A1 "File '.*avo_core/src" | grep -v "^--" | paste - -

clean:
	rm -rf $(BUILD)
