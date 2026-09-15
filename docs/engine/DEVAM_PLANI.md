# Tulpar Engine — Devam Planı (dil bağlaması HARİÇ)

> Kapsam: yalnız motorun kendisi. Tulpar dil bağlaması (`sim/` içine gameplay
> yazma köprüsü) bilinçli olarak bu planın dışında — ayrı, ayrı bir zamanda ele
> alınacak. Bu doküman `PLAN.md`/`DURUM.md`/`TULPAR_TAM_DOKUMAN_KONTROL.md`'nin
> yerine geçmiyor, onlardan çıkan "ne eksik" bulgusunu **öncelik sırasına**
> döküyor. Sıralama mantığı: bir sonraki fazın önkoşulu olmayan hiçbir şeye
> önce başlanmaz.

## Neden bu sıra

Şu ana kadar yapılan iş (İş 1-4,6,7,9) motoru **görsel olarak** ilerletti ama
motorun "oyun yapılabilir" olmasına katkısı sınırlı: sahneyi kaydedemiyorsunuz,
hiçbir şey karar vermiyor (AI yok), editörden nesne ekleyip silemiyorsunuz.
Bunlar olmadan ne kadar iyi görünürse görünsün, üstüne bir oyun kurulamaz.
Bu yüzden **Faz A önce** — her şeyin gerçek önkoşulu.

---

## Faz A — Sahne kalıcılığı (en yüksek öncelik)

Editör zaten var (Sahne+Özellikler panelleri, gizmo, oynat/durdur —
`app/editor_app.cpp`) ama veri kalıcı değil: entity'ler koda gömülü.

1. **Sahne dosya formatı** — insan tarafından okunabilir/diff'lenebilir (metin,
   bir entity bir dosya değilse bile en azından git-dostu), transform/mesh/
   malzeme/ışık/fizik alanlarını taşır. En kritik tek karar: format yanlış
   kurulursa editör+kaydet+ileride bake hepsi yeniden yazılır.
2. **Editöre kaydet/yükle** — mevcut `EditorEntity[]` dizisini dosyaya yaz/oku.
3. **Geri al (undo)** — basit bir komut geçmişi (her değişiklik = bir komut).
4. **Editörden ekle/sil** — UI'dan yeni entity oluşturma/silme (şu an yalnız
   var olan 5 entity arasında seçim yapılabiliyor).
5. **İçerik gezgini (temel)** — mevcut asset'leri (mesh/doku) listele, editörde
   sürükle-bırak yerine basit bir tıkla-ata yeterli.

**Bitiş kriteri:** editörde bir sahne kurup kaydedip kapatıp yeniden açtığınızda
aynı sahne geri geliyor.

## Faz B — Oynanabilirlik temelleri

Sahne kalıcı olduktan sonra üstüne "oyun" kurulabilir hale gelir.

6. **Basit davranış sistemi (AI)** — dil bağlaması olmadan, C++ tarafında basit
   bir state machine veya behavior tree. Navmesh zaten var (yol bulma); eksik
   olan "ne zaman saldır/kaç/devriye gez" kararı.
7. **Kamera sistemi** — takip, çarpışma-farkındalı, sarsıntı (screen shake),
   FOV geçişi. Basit görünür ama çoğu projenin gerçekte takıldığı yer.
8. **Tetikleyici hacimler (trigger)** — "oyuncu bu alana girince X olsun"
   — fizikten ayrı, gameplay'e ait bir kavram.
9. **Basit durum/envanter** (yalnız oyun tasarımı gerektiriyorsa).

**Bitiş kriteri:** navmesh ajanı oyuncuyu görünce kovalıyor, bir tetikleyici
alana girince bir şey tetikleniyor, kamera oyuncuyu çarpışmaya takılmadan
izliyor.

## Faz C — Görsel tamamlama (gerçek derleyici + cihaz gerektirir)

Bu oturumda yazılan ama derlenmemiş/kablolanmamış işler. **Sırası B'den sonra
değil, B ile PARALEL gidebilir** — Linux'ta ayrı biri/oturum bunu ilerletirken
A/B burada devam edebilir.

10. `python3 engine/tools/compile_shaders.py` çalıştır (tüm bekleyen shader'ları
    tek seferde derler: mesh/mesh_skin/shadow_skin/toon/foliage/vat/unlit).
11. İş 4/6/7'nin (PBR, sis, tonemap) gerçek ekran görüntüsüyle doğrulanması —
    şu ana kadar yalnız elle iz sürülerek doğrulandı.
12. Yeni 4 shader'ın (toon/foliage/vat/unlit) C++ kablolaması (her dosyanın
    başındaki yorumda adım adım yazılı).
13. Ertelenen İş 5 (yansıma probe), 8 (MSAA), 10 (normal map), 11 (AO),
    12 (contact shadow), 13 (bloom), 16 (oto-exposure/dynamic res).

## Faz D — İçerik ve ses

14. Gerçek karakter içeriği (sanatçı varlığı — modelleme/rigging elle ya da
    UniRig/SkinTokens ile, `TULPAR_TAM_DOKUMAN_KONTROL.md` §5).
15. 3D uzamsal ses (Steam Audio ya da miniaudio'nun basit pan/mesafe modeli).

## Faz E — Platform genişletme

16. Windows native port (7 dosya, daha önce çıkarıldı — `platform/thread.cpp`,
    `memory.cpp`, `window.cpp`, `time.cpp`, `crash.cpp`, `rhi/vk_api.cpp`,
    `core/jobs/fiber.cpp`).
17. Android GameActivity göçü (zaten `BOSLUK-TARAMASI.md`'de İP-P olarak
    planlıydı).

## Bilinçli olarak dışarıda bırakılan

- **Tulpar dil bağlaması** — kullanıcı isteğiyle bu planın kapsamı dışında.
- Çoklu oyunculu/ağ, iOS, mağaza entegrasyonu — ilk oynanabilir sürümden sonra.

---

## Önerilen sıradaki somut adım

**Faz A, madde 1-2: sahne formatı + kaydet/yükle.** Editör zaten çalışıyor,
tek eksik parça veri kalıcılığı. Bu, C++ tarafında (yeni shader/RHI riski yok)
güvenle ilerletilebilecek bir iş — bu oturumun geri kalanında buna devam
edilebilir.
