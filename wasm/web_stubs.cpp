// Web (Emscripten) runtime stub'ları.
//
// tulpar_async.cpp web arşivine giremez (stackful coroutine'ler ucontext
// ister; Emscripten'de yok). Ama AOT'un ürettiği main(), programın sonunda
// bekleyen async işleri bitirmek için aot_event_loop_run()'ı KOŞULSUZ
// çağırır — async kullanmayan programda bu zaten no-op'tur. Bu stub o tek
// koşulsuz referansı karşılar.
//
// Bilinçli olarak YALNIZ bunu stub'lıyoruz: async/await kullanan bir
// program web hedefinde aot_spawn / aot_await / ... sembollerinde link
// hatası alır — doğru davranış, çünkü async web runtime'ında desteklenmiyor
// (bkz. wasm/build_tame_web.sh notları).
extern "C" void aot_event_loop_run(void) {}
