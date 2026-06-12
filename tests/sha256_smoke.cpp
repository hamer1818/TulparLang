// Standalone smoke test for src/pkg/sha256. Compiled separately by
// CI to verify the SHA-256 helper produces FIPS 180-4 reference
// digests for the canonical test vectors. Build manually with:
//
//   g++ -std=c++17 tests/sha256_smoke.cpp src/pkg/sha256.cpp \
//       -Isrc/pkg -o sha256_smoke && ./sha256_smoke
//
// Exits 0 on success, 1 on any vector mismatch. Not run by build.sh
// test today (separate compile unit) — wired into CI as a tiny
// dedicated step.

#include "../src/pkg/sha256.hpp"
#include <cstdio>
#include <string>

namespace {

struct Vec {
    const char *input;
    const char *expected;
};

const Vec kVectors[] = {
    {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"abc",
     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
};

}  // namespace

int main() {
    int failures = 0;
    for (const auto &v : kVectors) {
        std::string got = tulpar::sha256_hex(v.input, std::char_traits<char>::length(v.input));
        if (got != v.expected) {
            std::fprintf(stderr, "FAIL: sha256(\"%s\") = %s (expected %s)\n",
                         v.input, got.c_str(), v.expected);
            failures++;
        } else {
            std::fprintf(stdout, "PASS: sha256(\"%s\") = %s\n", v.input, got.c_str());
        }
    }
    return failures == 0 ? 0 : 1;
}
