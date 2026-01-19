CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc
SRC_DIR = src
BUILD_DIR = build
TARGET = tulpar

# Detect OS
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lws2_32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        LDFLAGS = -lm -lpthread -ldl
    endif
    ifeq ($(UNAME_S),Darwin)
        LDFLAGS = -lpthread -ldl
    endif
endif

# Tüm kaynak dosyaları bul (recursive)
LEXER_SOURCES = $(wildcard $(SRC_DIR)/lexer/*.c)
PARSER_SOURCES = $(wildcard $(SRC_DIR)/parser/*.c)
INTERPRETER_SOURCES = $(filter-out $(SRC_DIR)/interpreter/thread_impl.c,$(wildcard $(SRC_DIR)/interpreter/*.c))
VM_SOURCES = $(wildcard $(SRC_DIR)/vm/*.c)
JIT_SOURCES = $(wildcard $(SRC_DIR)/jit/*.c)
MAIN_SOURCE = $(SRC_DIR)/main.c
SQLITE_SOURCE = lib/sqlite3/sqlite3.c

SOURCES = $(LEXER_SOURCES) $(PARSER_SOURCES) $(INTERPRETER_SOURCES) $(VM_SOURCES) $(JIT_SOURCES) $(MAIN_SOURCE) $(SQLITE_SOURCE)

# Object dosyalarını oluştur
LEXER_OBJECTS = $(LEXER_SOURCES:$(SRC_DIR)/lexer/%.c=$(BUILD_DIR)/lexer_%.o)
PARSER_OBJECTS = $(PARSER_SOURCES:$(SRC_DIR)/parser/%.c=$(BUILD_DIR)/parser_%.o)
INTERPRETER_OBJECTS = $(INTERPRETER_SOURCES:$(SRC_DIR)/interpreter/%.c=$(BUILD_DIR)/interpreter_%.o)
VM_OBJECTS = $(VM_SOURCES:$(SRC_DIR)/vm/%.c=$(BUILD_DIR)/vm_%.o)
JIT_OBJECTS = $(JIT_SOURCES:$(SRC_DIR)/jit/%.c=$(BUILD_DIR)/jit_%.o)
MAIN_OBJECT = $(BUILD_DIR)/main.o
SQLITE_OBJECT = $(BUILD_DIR)/sqlite3.o

OBJECTS = $(LEXER_OBJECTS) $(PARSER_OBJECTS) $(INTERPRETER_OBJECTS) $(VM_OBJECTS) $(JIT_OBJECTS) $(MAIN_OBJECT) $(SQLITE_OBJECT)

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Lexer modülü
$(BUILD_DIR)/lexer_%.o: $(SRC_DIR)/lexer/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Parser modülü
$(BUILD_DIR)/parser_%.o: $(SRC_DIR)/parser/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Interpreter modülü
$(BUILD_DIR)/interpreter_%.o: $(SRC_DIR)/interpreter/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# VM modülü
$(BUILD_DIR)/vm_%.o: $(SRC_DIR)/vm/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# JIT modülü
$(BUILD_DIR)/jit_%.o: $(SRC_DIR)/jit/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Main
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) -c $< -o $@

# SQLite
$(BUILD_DIR)/sqlite3.o: $(SQLITE_SOURCE)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: all
	./$(TARGET)

.PHONY: all clean run

