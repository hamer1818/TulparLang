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
| Jolt → fiber job sistemi | ✅ `FiberJoltJobs` (`JPH::JobSystemWithBarrier` uyarlayıcısı): Jolt job'ları bizim worker'larda, çekirdek aşırı aboneliği yok, aynı özet. Jolt havuzu 3.6 ms vs fiber 4.5–6.3 ms (300 adım, 65 gövde; küçük iş, job başına ek yük — kilitsiz kuyruk açık iş). **Fiber yığını:** Jolt'un çarpışma job'ları 64 KB'de bekçi sayfaya çarptı (SIGSEGV `ProcessBodyPair`), 128 KB geçti; varsayılan 256 KB oldu | `physics_runs_on_fiber_job_system_same_hash` |
| Jolt entegrasyonu | ✅ Jolt 5.3.0 vendored (`engine/third_party/jolt`, MIT), `JPH_CROSS_PLATFORM_DETERMINISTIC` (FMA kapalı), x86_64 SSE4.2 / arm64 NEON; `Physics` sarmalayıcısı (Jolt tipleri dışarı sızmaz), sayan ayırıcı kancası. 65 gövde × 300 adım: kutular zemine oturuyor (y=0.486). **Thread sayısından bağımsız aynı özet** (1/4/auto). ⚠️ Jolt'un kendi thread havuzu; fiber job sistemine bağlama sonraki adım | `physics_boxes_settle_on_floor`, `faz2_gate_physics_is_deterministic_across_thread_counts` |
| Platformlar arası determinizm (REV 8 sınaması) | ✅ **CI'da doğrulandı (2026-09-14):** altın özet `5dc41c2bddc345cb` Linux x86_64 (SSE4.2) ve macOS arm64 (NEON) aynı. İlk iki deneme farklıydı (`4087…`) ve ikisi de bizim hatamızdı: (1) `axis_angle`'ın sin/cos'u libm'e bağlıydı → sabit bitler; (2) AArch64 derleyicisi `a*b+c`'yi FMA'ya birleştiriyor → `-ffp-contract=off` (Jolt'un resmi derlemesi de kapatıyor; define tek başına yetmiyor). PLAN.md REV 8 gevşetildi, "Determinizm sözleşmesi" §11 altına yazıldı | `physics_cross_platform_golden_hash` |
| Animasyon (ACL sınıfı) + GPU skinning | ⬜ | — |
| Recast/Detour navmesh | ✅ recastnavigation 1.6.0 vendored (`engine/third_party/recast`, zlib; Recast + Detour, Crowd yok). `NavMesh`: `build()` = **bake** (Recast boru hattı, ayırma serbest, sahne derleyicisinin işi), `init_query()` düğüm havuzu bir kez, `find_path`/`raycast`/`nearest_point` sorguları **0 ayırma** (Detour kancası + global new ile ölçülür). Test sahnesi: zemin + 3 m duvar + geçit + ulaşılamaz ada: yol duvarın etrafından (5 nokta, 18.8 birim; düz 12), ada için yol yok/kısmi, raycast duvarı görüyor, bake süreç içinde deterministik (bake özeti bilgi; platformlar arası iddia yok, bake makinede yapılır) | `navmesh_*` (5) |
| GPU particle sim | ⬜ (çizim Faz 3/5) | — |

## Ölçüm (bilgi, yerel)

```
1000 tick, 500 entity (350 mover + 150 soldier), 3 asama; ozet dd75193359610b81 (seri = replay = paralel)
engine tests: 38 passed, 0 failed, 1 atlandi (dogrulama katmani yerelde yok)
```

## CI ölçümleri (2026-09-14, #320)

```
Linux lavapipe (4 cekirdek): 42/42, dogrulama katmani ETKIN 0 hata; fizik ozeti 5dc41c2bddc345cb;
  Jolt havuzu 19.8 ms, fiber job sistemi 16.3 ms (300 adim)
macOS arm64 (Apple Paravirtual, MoltenVK dogrudan): 42/42 (2 gorunur atlama: GPL yok, dogrulama katmani yok);
  fizik ozeti 5dc41c2bddc345cb; Jolt havuzu 25.1 ms, fiber 23.5 ms
Yerel 9800X3D (16 cekirdek): Jolt havuzu 3.6 ms, fiber 4.5-7.6 ms
```
Not: az çekirdekli koşucularda fiber job sistemi Jolt'un havuzundan **hızlı**, 16 çekirdekte yavaş — job başına ek yük çekirdek sayısıyla ölçekleniyor (kilitli kuyruk, spinlock); kilitsiz kuyruk açık iş.

## Açık iş
- **Jolt adım içi ayırma — kaynak bulundu:** `JPH::QuadTree::UpdatePrepare` (geniş faz ağacı yeniden kurulumu), her ~3 adımda 256 B + 1024 B (`TULPAR_ENGINE_JOLT_ALLOC_TRACE=1` çağrı yığınını modül+ofset basar, addr2line ile çözüldü). Jolt'un tasarımı; sınırlı ve kararlı. Test "adım başına ≤ 2" der. Sıfır istenirse Jolt'a yama ya da ağaç yeniden kurma sıklığı ayarı; şimdilik kabul.
- Kilitsiz iş kuyruğu: Jolt havuzuyla fark (3.6 vs ~5 ms) job başına ek yükte; ölçülmeden değiştirilmez.

## Tasarım notları
- **Belirlenimlilik sınırı:** aynı ikili + aynı mimari + aynı FP bayrakları (PLAN.md REV 8). Paralel aşama içindeki sistemler ayrık veriye dokunduğu için sıra önemsiz; **swap-remove sırayı değiştirir** — sistemler satır sırasına değil veriye bağlı olmalı (test dünyası buna uyar; kural Tuzaklar'a girecek).
- Yapısal değişiklik (create/destroy) bir sistemin içinde değil, tick sınırında yapılır (arcade'in "level switch deferred" kuralının aynısı).
- `content_hash` archetype/chunk/satır sırasıyla FNV-1a: replay testinin ölçüsü; ağ/persist için değil (Faz 6 reflection ile serializer gelir).
