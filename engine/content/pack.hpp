// L6 CONTENT — Basit varlik paketleme formati (500 madde listesi #457
// "Varlik Paketleme"). Duzen: [PackHeader][PackEntry * entry_count][ham
// veri bloklari, entry sirasiyla ARDISIK]. Amac: build zamaninda birden
// fazla kucuk dosyayi (doku/mesh/ses) TEK bir dosyada, isimle aranabilir
// sekilde birlestirmek — cok sayida kucuk dosya acmanin (mobilde APK icinde
// pahali) yerine.
//
// content/ katmani renderer/'a BAGIMLI DEGIL (PLAN.md katman kurali): saf
// bayt duzeni, ic verinin ne oldugunu (doku/mesh) BILMEZ.
#pragma once
#include <cstdint>

namespace tulpar::engine::content {

// "PACK" ASCII baytlarinin kucuk-uc-once (little-endian) uint32'si:
// 'P'=0x50,'A'=0x41,'C'=0x43,'K'=0x4B -> 0x4B434150.
constexpr uint32_t kPackMagic = 0x4B434150u;

struct PackHeader {
  uint32_t magic = kPackMagic;
  uint32_t version = 1;
  uint32_t entry_count = 0;
  uint32_t reserved = 0; // hizalama + ileride kullanim
};

struct PackEntry {
  char name[64] = {}; // NUL ile DOLDURULMUS sabit boyutlu isim (ayirma yok ilkesi)
  uint64_t offset = 0; // pack arabellegi icindeki BAYT ofseti (veri blogunun basi)
  uint64_t size = 0;
};

// build_pack() icin cagiranin ONCEDEN ayirmasi gereken TAM arabellek boyutu.
uint64_t compute_pack_size(const uint64_t *blob_sizes, uint32_t count);

// names[i]: NUL-sonlandirilmis, 63 karakteri asmayan isim (asarsa KESILIR).
// out_buffer: compute_pack_size() ile hesaplanan TAM boyutlu, cagiranin
// ayirdigi arabellek.
void build_pack(const char *const *names, const uint8_t *const *blobs, const uint64_t *blob_sizes, uint32_t count,
                 uint8_t *out_buffer);

// pack_buffer icinde `name` ile eslesen girdiyi arar. Bulunursa out_data/
// out_size doldurulur (pack_buffer'IN ICINE isaretci — KOPYALAMA yapilmaz),
// true doner. Gecersiz magic ya da bulunamayan isim icin false.
bool find_pack_entry(const uint8_t *pack_buffer, uint64_t pack_size, const char *name, const uint8_t **out_data,
                      uint64_t *out_size);

} // namespace tulpar::engine::content
