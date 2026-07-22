// Android (bionic) runtime stub'ları.
//
// tulpar_async.cpp Android arşivine giremez (stackful coroutine'ler
// makecontext/swapcontext ister; bionic bunları KALDIRDI — NDK r27
// sysroot'unda yok. Emscripten'dekiyle aynı sınırlama, bkz.
// wasm/web_stubs.cpp).
//
// runtime_bindings.cpp'nin async HTTP yolu (aot_http_request_async) bu
// promise/event-loop sembollerini referanslar; async KULLANMAYAN bir oyunda
// o yol ölü koddur ama linker sembolleri yine de ister. `.so` linkine
// -Wl,--no-undefined verdiğimiz için (yükleme anında değil, LİNK anında
// yakalanır) burada karşılamamız gerekir. Bir oyun gerçekten async çağırırsa
// (Android'de desteklenmiyor) abort eder — sessiz yanlış davranıştan iyidir.
#include "vm/vm.hpp"
#include <cstdio>
#include <cstdlib>

extern "C" {

// main() sonunda koşulsuz çağrılır; async yoksa zaten no-op.
void aot_event_loop_run(void) {}

[[noreturn]] static void async_unsupported(const char *fn) {
  fprintf(stderr,
          "Tulpar: '%s' cagrildi ama async Android hedefinde desteklenmiyor "
          "(bionic'te makecontext yok). / async is not supported on the "
          "Android target.\n",
          fn);
  abort();
}

ObjPromise *aot_promise_new(void) {
  async_unsupported("aot_promise_new");
}
void aot_promise_settle(ObjPromise *, VMValue, int) {
  async_unsupported("aot_promise_settle");
}
void aot_io_register(int (*)(void *), void *) {
  async_unsupported("aot_io_register");
}

} // extern "C"
