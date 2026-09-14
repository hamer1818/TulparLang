// L4 SIMULATION — Jolt'un JobSystem arayuzunu BIZIM fiber job sistemimize
// baglar (plan §1.6 "kendi job system'imize bagla"). Jolt'un thread havuzu
// olmadan: fizik job'lari ayni worker'larda kosar, cekirdekler asiri
// abone olmaz ve profiler'da "jolt:<ad>" bolgeleri gorunur.
//
// Jolt Job nesneleri sabit havuzdan (Jolt'un FixedSizeFreeList'i, kapasite
// init'te). Bariyer beklemesi Jolt'un JobSystemWithBarrier'inden: cagiran
// (Physics::step) bekler, job'lar beklemez.
#pragma once

#include "core/jobs/job_system.hpp"

namespace tulpar::engine::sim {

class Physics;
// physics.cpp icinde tanimli; Physics::init(cfg.jobs != nullptr) ile kullanilir.
// Bu baslik yalniz L4 icinde: Jolt tipleri disari sizmasin.

} // namespace tulpar::engine::sim
