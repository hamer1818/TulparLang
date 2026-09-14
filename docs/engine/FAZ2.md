# Faz 2 — Simülasyon: Durum

> Plan: `PLAN.md` §7 Faz 2 (eski Faz 5). Başladı 2026-09-14. Kod: `engine/sim/` (L4). Testler: `engine_tests ecs|sched|fixed|faz2|sim_tick`.
> Faz 1'in kapısı cihaz bekliyor (`FAZ1.md`); yazılım tarafı bittiği için Faz 2 paralel açıldı — plan "faz atlanmaz" der, kapı atlanmıyor, cihaz gelince ölçülür.

## Teslim edilen

| plan maddesi | durum | kanıt |
|---|---|---|
| ECS: archetype / SoA | ✅ `World`: archetype = bileşen kümesi, sabit boyutlu chunk'lar, chunk içinde SoA sütunlar; entity = nesil etiketli `Handle`; swap-remove; **bütün kapasiteler init'te arenadan** (entity, archetype, chunk havuzu); dolunca `create()` geçersiz handle + sayaç. ⚠️ "layout'u compiler üretir" §11 alt kümesine bağlı; bu C++ SoA aynı API | `ecs_create_get_destroy_and_generations`, `ecs_each_iterates_soa_columns` |
| Sistemler job olarak, bağımlılık grafiği | ✅ `Schedule`: sistem okuma/yazma maskesi bildirir; **aşamalar init'te** çıkarılır (yaz-yaz / yaz-oku / oku-yaz çakışması → sonraki aşama; kayıt sırası korunur = belirlenimli); aşama içi sistemler job sisteminde paralel | `schedule_builds_stages_from_read_write_sets` (input → move‖damage → bounds = 3 aşama) |
| Deterministik fixed-step | ✅ `FixedStep`: biriktirici, kare başına en çok N tick, fazlası **atılır ve sayılır** (sarmal yok) | `fixed_step_is_deterministic_and_clips` |
| Kayıt / replay | ✅ `InputRecorder`: tick başına sabit blok; replay aynı girdiyle aynı `content_hash` | **Faz 2 kapısı** `faz2_gate_replay_is_bit_identical`: 1000 tick × 500 entity, seri = replay = paralel (3 aşama); pozitif kontrol: tek girdi bozulunca özet değişiyor |
| Kare içinde 0 ayırma (sim) | ✅ 50 tick, 4 sistem, job'lı: `operator new` = 0 (sürücü yok, saf motor kodu) | `sim_tick_allocates_nothing` |
| Jolt entegrasyonu | ✅ Jolt 5.3.0 vendored (`engine/third_party/jolt`, MIT), `JPH_CROSS_PLATFORM_DETERMINISTIC` (FMA kapalı), x86_64 SSE4.2 / arm64 NEON; `Physics` sarmalayıcısı (Jolt tipleri dışarı sızmaz), sayan ayırıcı kancası. 65 gövde × 300 adım: kutular zemine oturuyor (y=0.486). **Thread sayısından bağımsız aynı özet** (1/4/auto). ⚠️ Jolt'un kendi thread havuzu; fiber job sistemine bağlama sonraki adım | `physics_boxes_settle_on_floor`, `faz2_gate_physics_is_deterministic_across_thread_counts` |
| Platformlar arası determinizm (REV 8 sınaması) | 🔬 altın özet x86_64'te `ed7d5c5d0986ca44`; CI macOS arm64 aynı çıkarsa Jolt'un iddiası bayraklarımızla tutuyor, REV 8 gevşetilir; çıkmazsa REV 8 kalır | `physics_cross_platform_golden_hash` |
| Animasyon (ACL sınıfı) + GPU skinning | ⬜ | — |
| Recast/Detour navmesh | ⬜ | — |
| GPU particle sim | ⬜ (çizim Faz 3/5) | — |

## Ölçüm (bilgi, yerel)

```
1000 tick, 500 entity (350 mover + 150 soldier), 3 asama; ozet dd75193359610b81 (seri = replay = paralel)
engine tests: 38 passed, 0 failed, 1 atlandi (dogrulama katmani yerelde yok)
```

## Açık iş
- **Jolt adım içi ayırma:** ısınma sonrası ~her 3 adımda bir 256 B + 1024 B çifti (`TULPAR_ENGINE_JOLT_ALLOC_TRACE=1` ile izlendi; 300 adımda 76 çift). Kaynak henüz bulunmadı (aday: contact cache / island splitter dizileri). Test şimdilik "adım başına ≤ 2" der; sıfıra indirmek için Jolt ayarları (`PhysicsSettings`, ön-boyutlandırma) incelenecek.
- Jolt job'larını fiber job sistemine bağlamak (`JPH::JobSystem` uyarlayıcısı).

## Tasarım notları
- **Belirlenimlilik sınırı:** aynı ikili + aynı mimari + aynı FP bayrakları (PLAN.md REV 8). Paralel aşama içindeki sistemler ayrık veriye dokunduğu için sıra önemsiz; **swap-remove sırayı değiştirir** — sistemler satır sırasına değil veriye bağlı olmalı (test dünyası buna uyar; kural Tuzaklar'a girecek).
- Yapısal değişiklik (create/destroy) bir sistemin içinde değil, tick sınırında yapılır (arcade'in "level switch deferred" kuralının aynısı).
- `content_hash` archetype/chunk/satır sırasıyla FNV-1a: replay testinin ölçüsü; ağ/persist için değil (Faz 6 reflection ile serializer gelir).
