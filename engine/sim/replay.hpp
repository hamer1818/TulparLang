// L4 SIMULATION — Deterministik girdi kaydi + tekrar oynatma. 500 madde
// listesinde dogrudan bir madde DEGIL, ama motorun ZATEN VAR OLAN
// determinizm avantajinin (VIZYON.md SS1: "3 platform bit-es") DOGAL,
// NEREDEYSE BEDAVA bir sonucu -- MAME4droid'in (bu oturumda dogrulandi)
// "boot fully deterministic... so only inputs ever cross the network"
// ilkesinin AYNISI, ag yerine DISKE uygulanmis hali.
//
// **DEVRIMSEL OLAN:** bir replay dosyasi YALNIZ GIRDI DIZISIDIR (kilobyte
// mertebesinde -- 1000 kare * birkac bayt girdi = birkac KB). Unity/Unreal
// gibi motorlarin video/state-based replay'leri (megabyte, platform-
// bagimli render'a dayanir) ile KIYASLANAMAYACAK kadar kucuk, VE
// HERHANGI bir platformda (x86_64/ARM/vb) yeniden oynatildiginda
// SIMULASYONUN KENDISI BIT-ES AYNI sonucu yeniden URETIR (kaydedilmis bir
// video DEGIL, GERCEK yeniden hesaplama) -- crash raporu ekleri (hatayi
// UZAKTAN, TAM OLARAK yeniden URETMEK icin), oyun-ici replay, "hayalet"
// (ghost) yarisci modu icin nerdeyse ucretsiz bir yetenek.
//
// Input: POD, trivially-copyable olmali (rollback.hpp/lag_compensation.hpp
// ile AYNI kisit). Kayit/oynatma TARAFLARI FARKLI bir Input boyutu
// kullanirsa (surum uyusmazligi) header'daki input_size alani BUNU TESPIT
// EDER, sessizce yanlis yorumlama YAPILMAZ.
#pragma once
#include <cstdint>
#include <cstring>

#include "core/memory/arena.hpp"

namespace tulpar::engine::sim {

// "RPLY" ASCII baytlarinin kucuk-uc-once (little-endian) uint32'su.
constexpr uint32_t kReplayMagic = 0x594C5052u;

struct ReplayHeader {
  uint32_t magic = kReplayMagic;
  uint32_t version = 1;
  uint32_t frame_count = 0;
  uint32_t input_size = 0; // sizeof(Input) -- yukleme ANINDA dogrulanir
};

template <class Input>
class ReplayRecorder {
 public:
  bool init(Arena &arena, uint32_t max_frames) {
    frames_ = arena.alloc_array<Input>(max_frames);
    capacity_ = max_frames;
    count_ = 0;
    return frames_ != nullptr;
  }

  // Kare basina TAM BIR KEZ, MONOTON sirayla cagrilir. Kapasite dolduysa
  // false doner (SESSIZCE kesme YOK -- cagiran kontrol eder).
  bool record(const Input &input) {
    if (count_ >= capacity_) return false;
    frames_[count_++] = input;
    return true;
  }

  uint32_t frame_count() const { return count_; }
  uint64_t serialized_size() const { return sizeof(ReplayHeader) + (uint64_t)count_ * sizeof(Input); }

  // out_buffer: cagiranin serialized_size() ile ONCEDEN ayirdigi tampon.
  void serialize(uint8_t *out_buffer) const {
    const ReplayHeader header{kReplayMagic, 1, count_, (uint32_t)sizeof(Input)};
    std::memcpy(out_buffer, &header, sizeof(header));
    if (count_ > 0) std::memcpy(out_buffer + sizeof(header), frames_, (size_t)count_ * sizeof(Input));
  }

 private:
  Input *frames_ = nullptr;
  uint32_t capacity_ = 0;
  uint32_t count_ = 0;
};

template <class Input>
class ReplayPlayer {
 public:
  // buffer/size: ReplayRecorder::serialize() ile uretilmis (baska bir
  // sureçten/diskten okunmus olabilir) veri -- BU nesne KOPYALAMAZ, `buffer`
  // bu nesnenin YASAM SURESI boyunca GECERLI kalmalidir.
  bool load(const uint8_t *buffer, uint64_t size) {
    if (size < sizeof(ReplayHeader)) return false;
    ReplayHeader header;
    std::memcpy(&header, buffer, sizeof(header));
    if (header.magic != kReplayMagic) return false;
    if (header.input_size != sizeof(Input)) return false; // TIP/SURUM UYUSMAZLIGI -- guvenli reddet
    const uint64_t expected = sizeof(ReplayHeader) + (uint64_t)header.frame_count * sizeof(Input);
    if (size < expected) return false; // kesilmis/bozuk dosya
    data_ = buffer + sizeof(ReplayHeader);
    frame_count_ = header.frame_count;
    return true;
  }

  uint32_t frame_count() const { return frame_count_; }
  bool get_input(uint32_t frame, Input *out) const {
    if (frame >= frame_count_) return false;
    std::memcpy(out, data_ + (size_t)frame * sizeof(Input), sizeof(Input));
    return true;
  }

 private:
  const uint8_t *data_ = nullptr;
  uint32_t frame_count_ = 0;
};

} // namespace tulpar::engine::sim
