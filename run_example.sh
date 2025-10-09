#!/bin/bash
# OLang örnek çalıştırıcı

if [ -z "$1" ]; then
    echo "Kullanım: ./run_example.sh <dosya.olang>"
    echo ""
    echo "Örnek:"
    echo "  ./run_example.sh examples/hello.olang"
    exit 1
fi

if [ ! -f "$1" ]; then
    echo "Hata: '$1' dosyası bulunamadı!"
    exit 1
fi

echo "OLang dosyası çalıştırılıyor: $1"
echo "=========================================="
echo ""

# TODO: Dosyadan okuma özelliği eklenecek
echo "Not: Şu anda sadece main.c içindeki örnek kod çalışıyor."
echo "Dosyadan okuma özelliği yakında eklenecek!"

