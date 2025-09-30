# Copyright © 2025 Mint teams
# Makefile for Kavin

# Platform settings
UNAME_S := $(shell uname -s)
TARGET_NAME = kavin
TARGET_EXT =
ASM_DEFINES =

ifeq ($(UNAME_S),Linux)
	AFLAGS = -f elf64
	ASM_DEFINES = -DSTAT_SYSCALL=4 -DKILL_SYSCALL=62
else ifeq ($(UNAME_S),Darwin)
	AFLAGS = -f macho64
	ASM_DEFINES = -DSTAT_SYSCALL=0x2000188 -DKILL_SYSCALL=0x2000025
else ifneq ($(findstring MINGW,$(UNAME_S)),) # For MSYS2/MinGW on Windows
	TARGET_EXT = .exe
	AFLAGS = -f win64
	ASM_DEFINES = -DSTAT_SYSCALL=5 # Note: Direct syscalls on Windows are complex.
endif

# Compilers and Assembler
CC = gcc
ASM = nasm

# Directories
SRC_DIR = src
OBJ_DIR = obj
DIST_DIR = dist

# Source files
C_SRCS := $(wildcard $(SRC_DIR)/*/*.c) $(wildcard $(SRC_DIR)/*.c)
ASM_SRCS := $(wildcard $(SRC_DIR)/*/*.asm)

# Object files
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SRCS))
OBJS += $(patsubst $(SRC_DIR)/%.asm,$(OBJ_DIR)/%.o,$(ASM_SRCS))

# Target executable
TARGET = $(DIST_DIR)/$(TARGET_NAME)$(TARGET_EXT)

# Flags
CFLAGS = -O3 -march=native -flto -Wall -Wextra -I$(SRC_DIR)
LDFLAGS = -flto

# Rules

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(DIST_DIR)
	@echo "LD  $@"
	@$(CC) $(LDFLAGS) -o $@ $^ # $^ represents all prerequisites (.o files)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	@echo "CC  $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
	@mkdir -p $(@D)
	@echo "ASM $<"
	@$(ASM) $(AFLAGS) $(ASM_DEFINES) -o $@ $<

clean:
	@rm -rf $(DIST_DIR) $(OBJ_DIR)