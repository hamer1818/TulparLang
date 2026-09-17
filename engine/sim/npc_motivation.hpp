// L4 SIMULATION — TD-Hatasi NPC Motivasyonu (Dopamin/Ogrenme temelli NPC).
// DEVAM_PLANI.md Faz 4 (Devrimsel) madde D11.
// 
// NPC'lerin sabit karar agaclari (BehaviorTree) disinda, eylemlerinden
// "ogrenerek" (Pekistirmeli Ogrenme - Reinforcement Learning) kararlarini
// degistirmesini saglar.
//
// Veri-Yonelimli: Q-degerleri matrisi ajanlar arasinda paylasilmaz, her ajan
// kendi motivasyon durumunu tutar. Ancak sabit tahsisli (Arena) mimariye
// uymasi icin kucuk ve hizli bir bellek modeline sahiptir.
#pragma once
#include <cstdint>

namespace tulpar::engine::sim {

// Olay/eylem uzayi kucuk tutulmalidir (ornegin: 0=Saldir, 1=Kac, 2=Devriye)
constexpr uint32_t kMaxActions = 8;
constexpr uint32_t kMaxStates = 16;

struct MotivationConfig {
  float learning_rate = 0.1f; // Alpha
  float discount_factor = 0.9f; // Gamma
  float exploration_rate = 0.2f; // Epsilon (Rastgele eylem secme orani)
};

struct NpcMotivationState {
  // Q-Table (State x Action)
  // Sabit tahsisli, dinamik allocation yok.
  float q_table[kMaxStates][kMaxActions] = {};
  
  uint32_t current_state = 0;
  uint32_t last_action = 0;
};

class NpcMotivationSystem {
 public:
  // Verilen durum icin en iyi eylemi (veya epsilon-greedy rastgele eylemi) secer
  // random_value: 0.0 ile 1.0 arasinda disaridan verilen rastgele deger (determinizm icin)
  uint32_t select_action(const NpcMotivationState& state, const MotivationConfig& cfg, float random_value) const;

  // Gerceklesen odulu (reward) kullanarak Q-tablosunu (TD-Hatasi ile) gunceller
  void update_q_value(NpcMotivationState* state, const MotivationConfig& cfg, uint32_t next_state, float reward);
};

} // namespace tulpar::engine::sim
