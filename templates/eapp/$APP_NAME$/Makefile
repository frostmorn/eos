# EOS Standalone ELF Application Makefile
APP_NAME ?= $APP_NAME$
ROOT_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ACTIVE_BOARD ?= $(shell cat $(ROOT_DIR)/.active_board 2>/dev/null || echo "lilka_v23")

# Detect architecture from active board
ifeq ($(findstring jc4880p4,$(ACTIVE_BOARD)),jc4880p4)
    TARGET ?= esp32p4
    TOOL_PREFIX ?= riscv32-esp-elf-
    ARCH_STRIP_FLAGS = --remove-section=.riscv.attributes
else
    TARGET ?= esp32s3
    TOOL_PREFIX ?= xtensa-esp32s3-elf-
    ARCH_STRIP_FLAGS = --remove-section=.xt.lit --remove-section=.xt.prop --remove-section=.xtensa.info
endif

# Toolchain discovery in PATH or ~/.espressif/tools
CC_BIN ?= $(shell which $(TOOL_PREFIX)gcc 2>/dev/null || find $(HOME)/.espressif/tools -name $(TOOL_PREFIX)gcc 2>/dev/null | head -n1)
STRIP_BIN ?= $(shell which $(TOOL_PREFIX)strip 2>/dev/null || find $(HOME)/.espressif/tools -name $(TOOL_PREFIX)strip 2>/dev/null | head -n1)

ifeq ($(CC_BIN),)
    CC_BIN = $(TOOL_PREFIX)gcc
endif
ifeq ($(STRIP_BIN),)
    STRIP_BIN = $(TOOL_PREFIX)strip
endif

CC := $(CC_BIN)
STRIP := $(STRIP_BIN)

CFLAGS ?= -O2 -nostartfiles -nostdlib -fPIC -shared -e app_main -Dmain=app_main \
          -fdata-sections -ffunction-sections -Wl,--gc-sections \
          -fvisibility=hidden -Wl,--strip-all -Wl,--strip-debug -Wl,--strip-discarded \
          -I$(ROOT_DIR)/main/include_override

STRIP_FLAGS = --strip-unneeded --remove-section=.comment --remove-section=.got.loc \
              --remove-section=.dynamic $(ARCH_STRIP_FLAGS)

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)
TARGET_ELF = $(APP_NAME).elf

all: $(TARGET_ELF)

$(TARGET_ELF): $(SRCS)
	@echo "Compiling $(TARGET_ELF) for $(TARGET) ($(ACTIVE_BOARD))..."
	$(CC) $(CFLAGS) $(SRCS) -o $@
	$(STRIP) $(STRIP_FLAGS) $@
	@echo "Build successful: $(TARGET_ELF) ($$(ls -lh $(TARGET_ELF) | awk '{print $$5}'))"

clean:
	rm -f $(TARGET_ELF) $(OBJS)

.PHONY: all clean
