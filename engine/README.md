# Tulpar Engine — `engine/`

Plan: `docs/engine/PLAN.md` (mimari, revize 2026-09-14) · cihaz matrisi: `docs/engine/CIHAZ-MATRISI.md` · Faz 0 durumu: `docs/engine/FAZ0.md`.

Ayrı ağaç: `tulpar` derleyicisine ve `tulpar_runtime`a linklenmez; ileride oyun ikilisine linklenir. Bugün L0 `platform/` ve L1 `core/` var (C++17, exception/RTTI yok, STL konteyneri yok — bkz. `docs/engine/PLAN.md` §11 dil kararı).

```
platform/   L0: fatal, time, os memory, thread, crash reporter
core/
  memory/   Arena ailesi, Pool<T> slotmap, AllocGate (A2 kapisi)
  jobs/     fiber (x86_64 + AArch64 asm), JobSystem
  profiler/ kare cizelgesi, bolgeler, Chrome trace
  containers/ Array, Span, HashMap, StaticString
  math/     Vec, Mat4, Quat
tests/      engine_tests (build.sh suites de kosturur)
tools/      layer_check.py (ihlal = build hatasi), symbolize.py (crash raporu)
```

Kurallar (mekanik): katman yalnız altını içerir; STL konteyneri yasak; kare içinde `new` yok (`AllocGate`); zaman ölçümleri bilgi basar, karar vermez.
