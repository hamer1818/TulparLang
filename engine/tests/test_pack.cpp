// content/pack.hpp: paketleme+arama round-trip'inin BAYT BIREBIR ayni
// veriyi geri verdigini kanitlar (asil risk ofset aritmetigidir).
#include <cstring>

#include "content/pack.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine::content;

ENGINE_TEST(pack_build_and_find_roundtrip_byte_exact) {
  const uint8_t blob0[3] = {1, 2, 3};
  const uint8_t blob1[4] = {4, 5, 6, 7};
  const char *names[2] = {"foo", "bar"};
  const uint8_t *blobs[2] = {blob0, blob1};
  const uint64_t sizes[2] = {3, 4};

  const uint64_t total = compute_pack_size(sizes, 2);
  CHECK(total == sizeof(PackHeader) + sizeof(PackEntry) * 2 + 3 + 4);

  uint8_t buf[512]; // A2: yigin, ayirma yok -- total'in kesinlikle sigdigi sabit boyut
  CHECK(total <= sizeof(buf));
  build_pack(names, blobs, sizes, 2, buf);

  const uint8_t *data;
  uint64_t size;
  CHECK(find_pack_entry(buf, total, "foo", &data, &size));
  CHECK(size == 3);
  CHECK(std::memcmp(data, blob0, 3) == 0);

  CHECK(find_pack_entry(buf, total, "bar", &data, &size));
  CHECK(size == 4);
  CHECK(std::memcmp(data, blob1, 4) == 0);

  CHECK(!find_pack_entry(buf, total, "yok", &data, &size)); // olmayan isim
}

ENGINE_TEST(pack_rejects_invalid_magic) {
  uint8_t garbage[64] = {0};
  const uint8_t *data;
  uint64_t size;
  CHECK(!find_pack_entry(garbage, sizeof(garbage), "foo", &data, &size));
}

ENGINE_TEST(pack_rejects_truncated_buffer) {
  const uint8_t blob0[3] = {1, 2, 3};
  const char *names[1] = {"foo"};
  const uint8_t *blobs[1] = {blob0};
  const uint64_t sizes[1] = {3};
  const uint64_t total = compute_pack_size(sizes, 1);
  uint8_t buf[256];
  CHECK(total <= sizeof(buf));
  build_pack(names, blobs, sizes, 1, buf);

  const uint8_t *data;
  uint64_t size;
  // Buffer boyutunu YALANCI kucuk bildir (kesilmis dosya simulasyonu).
  CHECK(!find_pack_entry(buf, sizeof(PackHeader), "foo", &data, &size));
}
