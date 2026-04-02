#!/bin/bash
cd /mnt/d/yazilim/Tulpar

echo "Çalıştırılacak örnek testleri başlatılıyor..."
echo "============================================="

for file in examples/*.tpr; do
    echo -n "[TEST] $file ... "
    # Provide dummy input for interactive models: "John", "10"
    # Timeout 2 seconds so server loops don't hang
    timeout 2 ./tulpar "$file" <<< $'John\n10\n' > /dev/null 2>&1
    exit_code=$?
    
    if [ $exit_code -eq 124 ]; then
        echo "⏳ ZAMAN AŞIMI (Sunucu veya Etkileşimli bekleyiş)"
    elif [ $exit_code -eq 0 ]; then
        echo "✅ BAŞARILI"
    elif [ $exit_code -eq 139 ]; then
        echo "❌ SEGMENTATION FAULT (Hata kodu: 139)"
    else
        echo "❌ BAŞARISIZ (Hata kodu: $exit_code)"
    fi
done

echo "============================================="
echo "Testler tamamlandı."
