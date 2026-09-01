# Yer tutucu sesler

Bu beş dosya **üretilmiş yer tutucudur** — sentezle (sinüs + zarf + gürültü)
kuruldu, bir ses kütüphanesinden alınmadı. Amaçları editörün oyun
şablonlarının **kutudan çıktığı gibi duyulabilir** olması: sessiz bir oyun
"ses nasıl eklenir" sorusunu hiç sordurmuyordu.

Kendi seslerinle değiştirmek için: editörde kural satırındaki **ses** düğmesine
bas, deseni kendi klasörüne çevir, TARA'ya bas.

| dosya | ne için |
|---|---|
| `altin.wav` | toplama (ACT_COLLECT) |
| `vurus.wav` | hasar / çarpma (ACT_DAMAGE, ACT_HURT) |
| `ates.wav` | mermi çıkışı |
| `kazandin.wav` | ACT_WIN |
| `kaybettin.wav` | ACT_LOSE |

Biçim: mono, 16-bit PCM, 22050 Hz (depodaki `tame_assets/beep.wav` ile aynı).
Lisans: bu depoyla aynı — üretilmiş oldukları için üçüncü taraf hakkı yok.
