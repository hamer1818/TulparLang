// Tiny SHA-256 helper used by `tulpar pkg install` to compute
// content checksums for the lockfile. We avoid pulling in a real
// crypto dep (OpenSSL is already linked but its EVP API for one-off
// hashes is more boilerplate than the inline implementation here);
// the algorithm fits in ~80 lines of C and is straight from FIPS
// 180-4. Not intended for password hashing or anything where the
// upstream attacks against bare SHA-256 matter — strictly content
// integrity for downloaded packages.

#ifndef TULPAR_PKG_SHA256_HPP
#define TULPAR_PKG_SHA256_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace tulpar {

// Hex-encoded (lowercase, 64 chars) SHA-256 digest of `data`.
std::string sha256_hex(const void *data, size_t size);

// Convenience overload for std::string-shaped buffers.
inline std::string sha256_hex(const std::string &s) {
  return sha256_hex(s.data(), s.size());
}

}  // namespace tulpar

#endif  // TULPAR_PKG_SHA256_HPP
