// L3 AUDIO — ana yol (master bus) DSP zinciri.
//
// Kurallar (plan Faz 4): cihaz callback'inde AYIRMA YOK, KILIT YOK. Dugumler
// SABIT kapasiteli dizide (kMaxNodes), calisma sirasi VERI: order_ tek bir
// 64-bit atomik kelimede 8 x 8-bit yuva indisi olarak yayinlanir, boylece oyun
// thread'i sirayi tek atomik yazmayla degistirir ve cihaz thread'i yirtik
// gormez. Parametreler atomik float; filtre durumu (state) YALNIZ cihaz
// thread'ine ait.
//
// Dugum turleri: Gain (kazanc), LowPass (tek kutuplu alcak gecirgen),
// Limiter (yumusak kirpma). Zincir bos baslar — varsayilan davranis degismez.
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>

#include "audio/spatial.hpp" // onepole_coeff, clampf

namespace tulpar::engine::audio {

enum class DspKind : uint8_t { None = 0, Gain = 1, LowPass = 2, Limiter = 3 };

class DspChain {
public:
  static constexpr uint32_t kMaxNodes = 8;
  static constexpr uint32_t kMaxChannels = 8;

  // --- Oyun thread'i -------------------------------------------------------
  int add_gain(float gain) { return add(DspKind::Gain, gain, 0.0f); }
  int add_lowpass(float cutoff_hz) { return add(DspKind::LowPass, cutoff_hz, 0.0f); }
  // ceiling: |cikis| < ceiling (tanh yumusak kirpma; esik altinda da hafif bastirir)
  int add_limiter(float ceiling) { return add(DspKind::Limiter, ceiling, 0.0f); }

  void set_enabled(int node, bool on) {
    if (node < 0 || (uint32_t)node >= kMaxNodes) return;
    nodes_[node].on.store(on ? 1u : 0u, std::memory_order_relaxed);
  }
  bool enabled(int node) const {
    if (node < 0 || (uint32_t)node >= kMaxNodes) return false;
    return nodes_[node].on.load(std::memory_order_relaxed) != 0;
  }
  void set_param(int node, float a, float b = 0.0f) {
    if (node < 0 || (uint32_t)node >= kMaxNodes) return;
    nodes_[node].a.store(a, std::memory_order_relaxed);
    nodes_[node].b.store(b, std::memory_order_relaxed);
  }
  float param(int node) const {
    if (node < 0 || (uint32_t)node >= kMaxNodes) return 0.0f;
    return nodes_[node].a.load(std::memory_order_relaxed);
  }
  DspKind kind(int node) const {
    if (node < 0 || (uint32_t)node >= kMaxNodes) return DspKind::None;
    return (DspKind)nodes_[node].kind.load(std::memory_order_relaxed);
  }
  uint32_t count() const { return count_.load(std::memory_order_relaxed); }

  // Zincir sirasi = VERI. Gecersiz indis yok sayilir; n > kMaxNodes kirpilir.
  void set_order(const uint8_t *nodes, uint32_t n) {
    const uint32_t have = count_.load(std::memory_order_relaxed);
    uint64_t ord = 0;
    uint32_t k = 0;
    for (uint32_t i = 0; i < n && k < kMaxNodes; i++) {
      if (nodes[i] >= have) continue;
      ord |= (uint64_t)nodes[i] << (k * 8);
      k++;
    }
    order_n_.store(0, std::memory_order_relaxed); // once zinciri bosalt (yirtik yok)
    order_.store(ord, std::memory_order_release);
    order_n_.store(k, std::memory_order_release);
  }
  uint32_t order(uint8_t *out, uint32_t cap) const {
    const uint32_t n = order_n_.load(std::memory_order_acquire);
    const uint64_t ord = order_.load(std::memory_order_relaxed);
    const uint32_t k = n < cap ? n : cap;
    for (uint32_t i = 0; i < k; i++) out[i] = (uint8_t)((ord >> (i * 8)) & 0xFF);
    return n;
  }
  // Tum zinciri kaldirir (dugumler kalir, sira bosalir).
  void bypass_all() { order_n_.store(0, std::memory_order_release); }
  void clear() {
    order_n_.store(0, std::memory_order_release);
    order_.store(0, std::memory_order_relaxed);
    count_.store(0, std::memory_order_relaxed);
    for (uint32_t i = 0; i < kMaxNodes; i++) {
      nodes_[i].kind.store(0, std::memory_order_relaxed);
      nodes_[i].on.store(1, std::memory_order_relaxed);
      nodes_[i].a.store(0.0f, std::memory_order_relaxed);
      nodes_[i].b.store(0.0f, std::memory_order_relaxed);
      for (uint32_t c = 0; c < kMaxChannels; c++) nodes_[i].state[c] = 0.0f;
    }
  }

  // --- Cihaz thread'i: 0 ayirma, 0 kilit ------------------------------------
  void process(float *buf, uint32_t frames, uint32_t channels, uint32_t rate) {
    uint32_t n = order_n_.load(std::memory_order_acquire);
    if (n == 0 || frames == 0 || channels == 0) return;
    if (n > kMaxNodes) n = kMaxNodes;
    const uint64_t ord = order_.load(std::memory_order_relaxed);
    const uint32_t ch = channels < kMaxChannels ? channels : kMaxChannels;
    const uint32_t total = frames * channels;
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t idx = (uint32_t)((ord >> (i * 8)) & 0xFF);
      if (idx >= kMaxNodes) continue;
      Node &nd = nodes_[idx];
      if (nd.on.load(std::memory_order_relaxed) == 0) continue;
      const float a = nd.a.load(std::memory_order_relaxed);
      switch ((DspKind)nd.kind.load(std::memory_order_relaxed)) {
      case DspKind::Gain:
        for (uint32_t k = 0; k < total; k++) buf[k] *= a;
        break;
      case DspKind::LowPass: {
        const float c = onepole_coeff(a, rate);
        if (c >= 1.0f) break;
        for (uint32_t f = 0; f < frames; f++)
          for (uint32_t cc = 0; cc < ch; cc++) {
            float &st = nd.state[cc];
            st += c * (buf[f * channels + cc] - st);
            buf[f * channels + cc] = st;
          }
        break;
      }
      case DspKind::Limiter: {
        const float ceil = a > 1e-6f ? a : 1e-6f;
        const float inv = 1.0f / ceil;
        for (uint32_t k = 0; k < total; k++) buf[k] = ceil * std::tanh(buf[k] * inv);
        break;
      }
      default:
        break;
      }
    }
  }

private:
  struct Node {
    std::atomic<uint32_t> kind{0};
    std::atomic<uint32_t> on{1};
    std::atomic<float> a{0.0f}, b{0.0f};
    float state[kMaxChannels] = {}; // yalniz cihaz thread'i dokunur
  };

  int add(DspKind k, float a, float b) {
    const uint32_t i = count_.load(std::memory_order_relaxed);
    if (i >= kMaxNodes) return -1;
    nodes_[i].kind.store((uint32_t)k, std::memory_order_relaxed);
    nodes_[i].on.store(1, std::memory_order_relaxed);
    nodes_[i].a.store(a, std::memory_order_relaxed);
    nodes_[i].b.store(b, std::memory_order_relaxed);
    for (uint32_t c = 0; c < kMaxChannels; c++) nodes_[i].state[c] = 0.0f;
    const uint32_t n = order_n_.load(std::memory_order_relaxed);
    uint64_t ord = order_.load(std::memory_order_relaxed);
    count_.store(i + 1, std::memory_order_relaxed);
    if (n < kMaxNodes) {
      ord |= (uint64_t)i << (n * 8);
      order_.store(ord, std::memory_order_release);
      order_n_.store(n + 1, std::memory_order_release);
    }
    return (int)i;
  }

  Node nodes_[kMaxNodes];
  std::atomic<uint32_t> count_{0};
  std::atomic<uint64_t> order_{0};   // 8 x 8-bit yuva indisi
  std::atomic<uint32_t> order_n_{0}; // zincirdeki dugum sayisi
};

} // namespace tulpar::engine::audio
