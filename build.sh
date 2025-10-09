#!/bin/bash
echo "========================================"
echo "OLang Modüler Derleme (Linux/Mac/WSL)"
echo "========================================"
echo ""

# Build dizinini oluştur
mkdir -p build

# Lexer modülü
echo "Lexer derleniyor..."
gcc -Wall -Wextra -g -Isrc -c src/lexer/lexer.c -o build/lexer.o

# Parser modülü
echo "Parser derleniyor..."
gcc -Wall -Wextra -g -Isrc -c src/parser/parser.c -o build/parser.o

# Interpreter modülü
echo "Interpreter derleniyor..."
gcc -Wall -Wextra -g -Isrc -c src/interpreter/interpreter.c -o build/interpreter.o

# Main
echo "Main derleniyor..."
gcc -Wall -Wextra -g -Isrc -c src/main.c -o build/main.o

# Executable oluştur
echo "Executable oluşturuluyor..."
gcc build/lexer.o build/parser.o build/interpreter.o build/main.o -o olang

echo ""
echo "========================================"
echo "Derleme tamamlandı!"
echo "Çalıştırmak için: ./olang"
echo "========================================"

