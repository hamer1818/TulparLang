// L1 CORE — Epoch-Based Reclamation (EBR)
// DEVAM_PLANI.md Faz 6 (Ileri Düzey Mimari) madde M1.
// 
// Multi-threaded lock-free sistemlerde (Orn: event_bus, lock-free queue)
// veri yarislarina engel olmak amaciyla kullanilir. Geleneksel kilit (mutex)
// yerine "donem" (epoch) mantigiyla calisir. Eger bir "donem" icinde bir
// nesne okumasi baslamissa, o nesnenin silinmesi o donem bitene kadar bekletilir.
// Bu sayede Use-After-Free (UAF) onlenir.
#pragma once
#include <atomic>
#include <cstdint>

namespace tulpar::engine::core {

using EbrDestructor = void (*)(void* ptr);

// Sistemi hazirlar (kullanilacak maksimum thread sayisina gore)
void ebr_init();
void ebr_shutdown();

// Her thread (job_system fiberleri haric donanim threadleri) baslarken kendini kaydetmelidir.
void ebr_register_thread();
void ebr_unregister_thread();

// Lock-free bir yapi okumasi yapilacaginda cagirilir.
void ebr_enter();
// Okuma bittiginde cagirilir.
void ebr_exit();

// Bir nesneyi silmek icin siraya koyar (emekliye ayirir).
// Ancak hicbir thread ona erismediginden emin olundugunda (global epoch ilerlediginde)
// gercekten dtor cagirilir.
void ebr_retire(void* ptr, EbrDestructor dtor);

// Emekliye ayrilan ve artik erisilmeyen nesneleri gercekten siler.
// Engine'in ana frame dongusunde (schedule.cpp veya frame_gate.cpp) her kare bir kez cagrilabilir.
void ebr_flush();

// --- Gorunurluk: SESSIZ basarisizlik YOK ------------------------------
// Emeklilik havuzu dolarsa nesne, cokmek yerine BILINCLI olarak sizdirilir
// (dtor'u hemen cagirmak use-after-free olurdu). Bu sayac onu gorunur yapar;
// sifirdan buyukse kMaxRetired yetersizdir.
uint32_t ebr_dropped();
// kMaxThreads asildi: kaydolamayan thread KORUMASIZDIR.
uint32_t ebr_thread_overflow();

} // namespace tulpar::engine::core
