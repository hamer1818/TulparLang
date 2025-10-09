@echo off
echo ========================================
echo OLang Derleyici (Windows)
echo ========================================
echo.

REM Build dizinini oluştur
if not exist build mkdir build

REM Kaynak dosyaları derle
echo Dosyalar derleniyor...
gcc -Wall -Wextra -g -Isrc -c src/lexer.c -o build/lexer.o
gcc -Wall -Wextra -g -Isrc -c src/parser.c -o build/parser.o
gcc -Wall -Wextra -g -Isrc -c src/interpreter.c -o build/interpreter.o
gcc -Wall -Wextra -g -Isrc -c src/main.c -o build/main.o

REM Executable oluştur
echo Executable olusturuluyor...
gcc build/lexer.o build/parser.o build/interpreter.o build/main.o -o olang.exe

echo.
echo ========================================
echo Derleme tamamlandi!
echo Calistirmak icin: olang.exe
echo ========================================

