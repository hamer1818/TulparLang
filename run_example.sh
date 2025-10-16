#!/bin/bash
# TulparLang örnek çalıştırıcı

if [ -z "$1" ]; then
    echo "Kullanım: ./run_example.sh <dosya.tpr>"
    echo ""
    echo "Örnek:"
    echo "  ./run_example.sh examples/hello.tpr"
    exit 1
fi

if [ ! -f "$1" ]; then
    echo "Hata: '$1' dosyası bulunamadı!"
    exit 1
fi

echo "TulparLang dosyası çalıştırılıyor: $1"
echo "=========================================="
echo ""

# TODO: Dosyadan okuma özelliği eklenecek
echo "Not: Şu anda sadece main.c içindeki örnek kod çalışıyor."
echo "Dosyadan okuma özelliği yakında eklenecek!"

