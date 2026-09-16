#include "content/pack.hpp"

#include <cstring>

namespace tulpar::engine::content {

uint64_t compute_pack_size(const uint64_t *blob_sizes, uint32_t count) {
  uint64_t data_size = 0;
  for (uint32_t i = 0; i < count; i++) data_size += blob_sizes[i];
  return sizeof(PackHeader) + sizeof(PackEntry) * (uint64_t)count + data_size;
}

void build_pack(const char *const *names, const uint8_t *const *blobs, const uint64_t *blob_sizes, uint32_t count,
                 uint8_t *out_buffer) {
  PackHeader header;
  header.entry_count = count;
  std::memcpy(out_buffer, &header, sizeof(header));

  uint8_t *entries_ptr = out_buffer + sizeof(PackHeader);
  uint8_t *data_ptr = entries_ptr + sizeof(PackEntry) * (uint64_t)count;
  uint64_t running_offset = sizeof(PackHeader) + sizeof(PackEntry) * (uint64_t)count;

  for (uint32_t i = 0; i < count; i++) {
    PackEntry e{};
    uint32_t j = 0;
    for (; j < 63 && names[i][j] != '\0'; j++) e.name[j] = names[i][j];
    for (; j < 64; j++) e.name[j] = '\0';
    e.offset = running_offset;
    e.size = blob_sizes[i];
    std::memcpy(entries_ptr + sizeof(PackEntry) * (uint64_t)i, &e, sizeof(e));
    if (blob_sizes[i] > 0) std::memcpy(data_ptr, blobs[i], blob_sizes[i]);
    data_ptr += blob_sizes[i];
    running_offset += blob_sizes[i];
  }
}

namespace {
// stored: TAM 64 bayt, NUL-doldurulmus. query: cagiranin NUL-sonlandirilmis
// ismi. Standart strcmp mantigi: iki taraf da AYNI indekste '\0' bulursa
// eslesir, o noktadan SONRASI okunmaz (query'nin kendi tamponunun disina
// TASMAZ -- 'a' zaten '\0' oldugunda dongu HEMEN doner, bir sonraki
// query[i] okunmadan once).
bool name_matches(const char *stored, const char *query) {
  for (uint32_t i = 0; i < 64; i++) {
    const char a = stored[i];
    const char b = query[i];
    if (a != b) return false;
    if (a == '\0') return true;
  }
  return true; // 64 karakter TAM doldu, hicbir NUL'a rastlanmadan esit kaldi
}
} // namespace

bool find_pack_entry(const uint8_t *pack_buffer, uint64_t pack_size, const char *name, const uint8_t **out_data,
                      uint64_t *out_size) {
  if (pack_size < sizeof(PackHeader)) return false;
  PackHeader header;
  std::memcpy(&header, pack_buffer, sizeof(header));
  if (header.magic != kPackMagic) return false;

  const uint64_t entries_bytes = sizeof(PackEntry) * (uint64_t)header.entry_count;
  if (pack_size < sizeof(PackHeader) + entries_bytes) return false; // kesilmis/bozuk pack

  const uint8_t *entries_ptr = pack_buffer + sizeof(PackHeader);
  for (uint32_t i = 0; i < header.entry_count; i++) {
    PackEntry e;
    std::memcpy(&e, entries_ptr + sizeof(PackEntry) * (uint64_t)i, sizeof(e));
    if (!name_matches(e.name, name)) continue;
    if (e.offset + e.size > pack_size) return false; // bozuk giris (sinir disina tasar)
    *out_data = pack_buffer + e.offset;
    *out_size = e.size;
    return true;
  }
  return false;
}

} // namespace tulpar::engine::content
