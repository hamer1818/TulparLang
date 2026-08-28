---
tags: [moc, root]
---

# 🐎 TulparLang — Zihin Haritası

Statically-typed, **AOT-derlenen** dil. C++17 + **LLVM 18** backend. Kaynak uzantısı `.tpr`. Türkçe yazılmış; kullanıcıya görünen string'ler `src/common/localization.hpp` (`tr_en`) üzerinden hem TR hem EN.

> Bu vault, projeyi baştan taramak yerine hızlı başvuru içindir. Her not = bir konu + ilgili dosya yolları + diğer notlara wiki-bağlantılar. Tek doğruluk kaynağı yine de [STATUS.md](../../STATUS.md) ve [CLAUDE.md](../../CLAUDE.md).
>
> **Sürüm kontrolünde** (2026-08-28'den beri; öncesinde local-only'di). Yeni bir
> şey öğrenildiğinde buraya yazılır — yalnız commit mesajına değil. Obsidian'ın
> `workspace.json`'u hariç: her panel hareketinde değişiyor, içeriğe dair bir
> şey söylemiyor.

## Ana dallar
- [[Architecture]] — derleme pipeline'ı (Lexer → Parser → Typeinfer → AOT/LLVM → Runtime)
- [[Standard Library]] — `lib/*.tpr` gömülü stdlib
- [[Wings]] — HTTP framework (en aktif geliştirilen parça)
- [[Tame]] — oyun kütüphanesi (`import "tame"`, vendored raylib; 2B **ve** 3B bağlamalar)
- [[Arcade]] — 2B preset motoru (`import "arcade"`, saf Tulpar)
- [[Scene3D]] — **3B motor** (`import "scene3d"`, saf Tulpar; entity/çarpışma/kamera/arazi)
- [[Editor]] — **TameEngine**, sahne editörü (motorun içinde, saf Tulpar; dock/panel/dosya)
- [[Build System]] — CMake, `build.sh`, gömülü lib üretimi, installer
- [[Decisions]] — mimari kararlar (ADR): AOT-only, port 8484, arena modeli
- [[Performance]] — benchmark + stres testi sonuçları
- [[Testing]] — koşumlar + **bozma disiplini** (her düzeltme enjekte edilerek doğrulanır)
- [[Tuzaklar]] — ⚠️ **bir sorun çıktığında İLK bakılacak yer**: tekrar eden hata sınıfları
- [[Roadmap]] — açık eksikler / sonraki adımlar

## Çekirdek bileşenler
[[Lexer]] · [[Parser]] · [[Type Inference]] · [[AOT Backend]] · [[Runtime]] · [[Memory Model]] · [[Async Runtime]] · [[SQLite and DB]]

## Yardımcı altsistemler
[[LSP]] · [[Tooling]] (fmt/pkg/update) · [[Imports and Modules]] · [[Cross-platform]] · [[Android]]

## Çalıştırma
`tulpar script.tpr` → AOT derler + çalıştırır (**tek yürütme yolu**; AOT hatası = hard error). `tulpar build` standalone binary üretir. CLI dispatch: `src/main.cpp`.
