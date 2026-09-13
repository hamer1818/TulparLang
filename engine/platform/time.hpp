// L0 PLATFORM — monoton saat. Profiler ve kare zamanlamasi buradan okur.
#pragma once
#include <cstdint>

namespace tulpar::engine::platform {

// Monoton, nanosaniye. Mutlak degeri anlamsiz; yalniz farklar kullanilir.
uint64_t now_ns();

} // namespace tulpar::engine::platform
