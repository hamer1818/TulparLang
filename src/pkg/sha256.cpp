// Vendored minimal SHA-256. Public-domain style implementation
// (FIPS 180-4); not the fastest in the world, but `tulpar pkg
// install` hashes at most a handful of small files per run, so the
// throughput difference vs OpenSSL EVP is irrelevant compared to the
// integration cost. See sha256.hpp for the rationale.

#include "sha256.hpp"

#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void process_block(uint32_t state[8], const uint8_t block[64]) {
  uint32_t W[64];
  for (int i = 0; i < 16; ++i) {
    W[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
           (uint32_t)block[i * 4 + 2] << 8 | (uint32_t)block[i * 4 + 3];
  }
  for (int i = 16; i < 64; ++i) {
    uint32_t s0 = rotr(W[i - 15], 7) ^ rotr(W[i - 15], 18) ^ (W[i - 15] >> 3);
    uint32_t s1 = rotr(W[i - 2], 17) ^ rotr(W[i - 2], 19) ^ (W[i - 2] >> 10);
    W[i] = W[i - 16] + s0 + W[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

  for (int i = 0; i < 64; ++i) {
    uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + K[i] + W[i];
    uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + maj;
    h = g; g = f; f = e;
    e = d + t1;
    d = c; c = b; b = a;
    a = t1 + t2;
  }
  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

}  // namespace

namespace tulpar {

std::string sha256_hex(const void *data, size_t size) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

  const uint8_t *p = static_cast<const uint8_t *>(data);
  uint64_t total_bits = (uint64_t)size * 8;

  // Process full 64-byte blocks.
  while (size >= 64) {
    process_block(state, p);
    p += 64;
    size -= 64;
  }

  // Final block: copy tail, append 0x80, pad to 56 mod 64, then 8 bytes
  // of length in bits big-endian. If the tail has >55 bytes, an extra
  // block is needed.
  uint8_t final_block[128] = {0};
  std::memcpy(final_block, p, size);
  final_block[size] = 0x80;
  size_t pad_target = (size < 56) ? 64 : 128;
  for (int i = 0; i < 8; ++i) {
    final_block[pad_target - 1 - i] = (uint8_t)(total_bits >> (8 * i));
  }
  process_block(state, final_block);
  if (pad_target == 128) {
    process_block(state, final_block + 64);
  }

  char hex[65];
  for (int i = 0; i < 8; ++i) {
    std::snprintf(hex + i * 8, 9, "%08x", state[i]);
  }
  hex[64] = '\0';
  return std::string(hex);
}

}  // namespace tulpar
