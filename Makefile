# Makefile for Kavin project

# Configuration
# C Compiler
CC = gcc

# Compiler flags:
# -O3: Optimization level 3
# -march=native: Optimize for the host architecture
# -flto: Link-time optimization
# -Isrc: Add 'src' directory to the include search path
CFLAGS = -O3 -march=native -flto -Isrc

# Linker flags (none needed for this project currently)
LDFLAGS =

# Project directories
SRCDIR = src
OBJDIR = obj

# Executable name
TARGET = kavin

# --- Source and Object File Generation ---
# Find all .c files in the source directory and its subdirectories
SOURCES := $(shell find $(SRCDIR) -name "*.c")

# Generate corresponding object file paths in the object directory
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

# --- Build Rules ---
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

# Pattern rule to compile each .c file into a .o file
# $(OBJDIR)/%.o: $(SRCDIR)/%.c
# 	@mkdir -p $(@D) # Create the subdirectory in obj/ if it doesn't exist
# 	@echo "Compiling $<..."
# 	$(CC) $(CFLAGS) -c $< -o $@

# Rule to compile C source files into object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@) # Ensure the output directory exists
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule: remove generated executable and object files
clean:
	@echo "Cleaning up build artifacts..."
	@rm -f $(TARGET)
	@rm -rf $(OBJDIR)