# Makefile — c-coroutine build system
#
# Targets:
#   make           build the static library (lib/libcoroutine.a)
#   make tests     build and run all tests
#   make examples  build example binaries
#   make bench     build and run the benchmark
#   make clean     remove build artefacts

CC      := gcc
AR      := ar
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -Wshadow \
           -Wstrict-prototypes -Wmissing-prototypes \
           -g -O2
IFLAGS  := -Iinclude

# ── platform detection ────────────────────────────────────────────────────
ARCH := $(shell uname -m)
OS   := $(shell uname -s)

ifeq ($(ARCH),x86_64)
  USE_ASM := 1
else
  USE_ASM := 0
endif

# ── source files ──────────────────────────────────────────────────────────
LIB_SRCS_C := src/coroutine.c
ifeq ($(USE_ASM),1)
  LIB_SRCS_S := src/asm_ctx.S
  CFLAGS     += -DUSE_ASM_CTX=1
else
  LIB_SRCS_S :=
  CFLAGS     += -DUSE_ASM_CTX=0
endif

LIB_OBJS := $(LIB_SRCS_C:.c=.o) $(LIB_SRCS_S:.S=.o)

# ── directories ───────────────────────────────────────────────────────────
LIB_DIR  := lib
BIN_DIR  := bin

$(LIB_DIR) $(BIN_DIR):
	mkdir -p $@

# ── library ───────────────────────────────────────────────────────────────
LIB := $(LIB_DIR)/libcoroutine.a

$(LIB): $(LIB_OBJS) | $(LIB_DIR)
	$(AR) rcs $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(IFLAGS) -c $< -o $@

src/%.o: src/%.S
	$(CC) -c $< -o $@

all: $(LIB)

# ── tests ─────────────────────────────────────────────────────────────────
TEST_SRCS := tests/test_basic.c tests/test_pipeline.c tests/test_stress.c
TEST_BINS := $(patsubst tests/%.c, $(BIN_DIR)/%, $(TEST_SRCS))

$(BIN_DIR)/%: tests/%.c $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(IFLAGS) $< -L$(LIB_DIR) -lcoroutine -o $@

.PHONY: tests
tests: $(TEST_BINS)
	@echo "--- running tests ---"
	@for t in $(TEST_BINS); do \
	    echo "Running $$t"; \
	    $$t || exit 1; \
	done
	@echo "--- all tests passed ---"

# ── examples ──────────────────────────────────────────────────────────────
EXAMPLE_SRCS := examples/generator.c
EXAMPLE_BINS := $(patsubst examples/%.c, $(BIN_DIR)/%, $(EXAMPLE_SRCS))

$(BIN_DIR)/generator: examples/generator.c $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(IFLAGS) $< -L$(LIB_DIR) -lcoroutine -o $@

.PHONY: examples
examples: $(EXAMPLE_BINS)
	@echo "--- running examples ---"
	@$(BIN_DIR)/generator

# ── clean ─────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(LIB_DIR) $(BIN_DIR) src/*.o

.DEFAULT_GOAL := all
