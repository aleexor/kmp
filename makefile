# Usage:
# make kmp		# compile binary
# make clean	# remove binary and objects

# Compiler
CC := mpicc

# Compile flags
CFLAGS := -fopenmp -O3 -Wall

# Linker flags
LDFLAGS := -fopenmp

# Libraries
LDLIBS := -lpcap -lm

# Source directory
SRC_DIR := src

# Objects directory
OBJ_DIR := obj

# Executable name
BIN_NAME = kmp

# Build directory
BIN_DIR := .

# Source files
SRC = $(wildcard $(SRC_DIR)/*.c)

# Object files
OBJ = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Executable path
TARGET = $(BIN_DIR)/$(BIN_NAME)

$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	mkdir -p obj
	$(CC) -c $(CFLAGS) $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

# Default action: build all
all: $(TARGET)

# CLean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all