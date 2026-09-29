# AGENTS.md

Bu depoda çalışan her ajan için **tek kaynak `CLAUDE.md`**. Oradaki derleme,
test, kapı ve mimari notları bütün ajanlar için geçerli; bu dosya yalnız ona
yönlendiriyor.

Neden kısa (2026-09-28): bu dosya eskiden `CLAUDE.md`'nin elle tutulan ayrı
bir kopyasıydı (başlığı bile "# CLAUDE.md" idi) ve ondan ayrıştı. Şunları
söylüyordu, hepsi yanlıştı:

- "Native Windows is not supported", "no `build-windows` CI job": Windows
  2026-09-21/22'de geri geldi (#340/#341) ve CI'da derlenip koşuyor.
- `*_smoke.py` harness'leri elle koşulur, test adımlarını yalnız Linux işi
  koşar: hepsi `build.sh suites` içinde, üç CI işinde koşuyor.
- VM geri düşüşü, `--vm`, `--repl`: 3.13.0'da kaldırıldı, derleyici
  yalnız AOT.
- "registry deps are still TODO": `path:`, `url:` ve semver registry
  bağımlılıkları kuruluyor.
- "`obj.method()` desteklenmiyor": PR #44'ten beri her alıcıda çalışıyor.

İki kopya bir kez daha ayrışmasın diye içerik burada tekrarlanmıyor.

Kısa yol:

- Derleme: `./build.sh` (Linux/macOS; Windows'ta MSYS2 MINGW64 kabuğunda).
- Testler: `./build.sh test` (örnekler) ve `./build.sh suites` (paketler ve
  kapılar). Pencere açan hiçbir şeyi `DISPLAY=` olmadan koşmayın.
- Belge ve commit dili Türkçe; ayrıntı ve bütün kurallar `CLAUDE.md`'de.
