// L0 PLATFORM — isletim sisteminden bellek. YALNIZ acilista cagrilir
// (SystemArena rezervi, fiber yiginlari). Kare icinde cagrilmaz (A2).
#pragma once
#include <cstddef>

namespace tulpar::engine::platform {

size_t os_page_size();
// Okunur-yazilir anonim bellek; sayfa hizali. Basarisizlikta nullptr.
void *os_reserve(size_t bytes);
void os_release(void *p, size_t bytes);
// Bir sayfa araligini erisilmez yapar (fiber yigini tasma bekcisi).
bool os_protect_none(void *p, size_t bytes);

} // namespace tulpar::engine::platform
