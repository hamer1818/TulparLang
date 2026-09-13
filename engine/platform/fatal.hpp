// L0 PLATFORM — olumcul hata ve iddia. Motorda exception YOK (Tasarim Aksiyomu
// A2/A7 + "Exception ve RTTI yok"): hata yolu ya donus degeri ya da burasi.
#pragma once

namespace tulpar::engine::platform {

// Mesaji stderr'e basar ve sureci abort eder. Geri donmez.
[[noreturn]] void fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

} // namespace tulpar::engine::platform

// Kosul saglanmazsa fatal. Release'te de ACIK: motorun iddialari yalnizca
// gelistirici makinesinde degil, sahada da gecerli olmali (A7: hitch = bug,
// sessiz bozulma = daha kotu bug).
#define ENGINE_ASSERT(cond)                                                     \
  do {                                                                          \
    if (!(cond))                                                                \
      ::tulpar::engine::platform::fatal("ASSERT %s:%d: %s", __FILE__, __LINE__, \
                                        #cond);                                 \
  } while (0)

#define ENGINE_ASSERT_MSG(cond, ...)                                            \
  do {                                                                          \
    if (!(cond)) ::tulpar::engine::platform::fatal(__VA_ARGS__);                \
  } while (0)
