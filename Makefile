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

# LLVM Configuration
LLVM_CONFIG = llvm-config
LLVM_CFLAGS := $(shell $(LLVM_CONFIG) --cflags)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags --libs core analysis native --system-libs)

CFLAGS += $(LLVM_CFLAGS) -DTULPAR_AOT_ENABLED
LDFLAGS += $(LLVM_LDFLAGS)

# Tüm kaynak dosyaları bul (recursive)
LEXER_SOURCES = $(wildcard $(SRC_DIR)/lexer/*.c)
PARSER_SOURCES = $(wildcard $(SRC_DIR)/parser/*.c)
INTERPRETER_SOURCES = $(filter-out $(SRC_DIR)/interpreter/thread_impl.c,$(wildcard $(SRC_DIR)/interpreter/*.c))
VM_SOURCES = $(filter-out $(SRC_DIR)/vm/vm_stub.c,$(wildcard $(SRC_DIR)/vm/*.c))
JIT_SOURCES = $(wildcard $(SRC_DIR)/jit/*.c)
AOT_SOURCES = $(SRC_DIR)/aot/aot_pipeline.c $(SRC_DIR)/aot/llvm_backend.c $(SRC_DIR)/aot/llvm_types.c $(SRC_DIR)/aot/llvm_values.c
MAIN_SOURCE = $(SRC_DIR)/main.c
SQLITE_SOURCE = lib/sqlite3/sqlite3.c
CJSON_SOURCE = runtime/cJSON.c

SOURCES = $(LEXER_SOURCES) $(PARSER_SOURCES) $(INTERPRETER_SOURCES) $(VM_SOURCES) $(JIT_SOURCES) $(AOT_SOURCES) $(MAIN_SOURCE) $(SQLITE_SOURCE) $(CJSON_SOURCE)

# Object dosyalarını oluştur
LEXER_OBJECTS = $(LEXER_SOURCES:$(SRC_DIR)/lexer/%.c=$(BUILD_DIR)/lexer_%.o)
PARSER_OBJECTS = $(PARSER_SOURCES:$(SRC_DIR)/parser/%.c=$(BUILD_DIR)/parser_%.o)
INTERPRETER_OBJECTS = $(INTERPRETER_SOURCES:$(SRC_DIR)/interpreter/%.c=$(BUILD_DIR)/interpreter_%.o)
VM_OBJECTS = $(VM_SOURCES:$(SRC_DIR)/vm/%.c=$(BUILD_DIR)/vm_%.o)
JIT_OBJECTS = $(JIT_SOURCES:$(SRC_DIR)/jit/%.c=$(BUILD_DIR)/jit_%.o)
AOT_OBJECTS = $(AOT_SOURCES:$(SRC_DIR)/aot/%.c=$(BUILD_DIR)/aot_%.o)
MAIN_OBJECT = $(BUILD_DIR)/main.o
SQLITE_OBJECT = $(BUILD_DIR)/sqlite3.o
CJSON_OBJECT = $(BUILD_DIR)/cJSON.o

OBJECTS = $(LEXER_OBJECTS) $(PARSER_OBJECTS) $(INTERPRETER_OBJECTS) $(VM_OBJECTS) $(JIT_OBJECTS) $(AOT_OBJECTS) $(MAIN_OBJECT) $(SQLITE_OBJECT) $(CJSON_OBJECT)

TARGET_LIB = $(BUILD_DIR)/libtulpar_runtime.a

all: $(BUILD_DIR) $(TARGET) $(TARGET_LIB)

$(TARGET_LIB): $(VM_OBJECTS) $(LEXER_OBJECTS) $(PARSER_OBJECTS) $(JIT_OBJECTS) $(SQLITE_OBJECT) $(CJSON_OBJECT)
	ar rcs $@ $^

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

# cJSON
$(BUILD_DIR)/cJSON.o: $(CJSON_SOURCE)
	$(CC) $(CFLAGS) -c $< -o $@

# AOT modülü
$(BUILD_DIR)/aot_%.o: $(SRC_DIR)/aot/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: all
	./$(TARGET)

.PHONY: all clean run

