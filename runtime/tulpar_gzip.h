// tulpar_gzip — in-tree gzip (RFC 1952) compressor for the AOT runtime.
// DEFLATE (RFC 1951) with fixed Huffman codes + greedy LZ77 over a 32K
// window. No external dependency (same philosophy as the in-tree SHA-256 /
// PBKDF2): user binaries stay self-contained, no -lz at AOT link time.
//
// Compression ratio is below zlib's dynamic-Huffman output (fixed codes,
// greedy matching) but ample for the intended use — HTTP response bodies
// (JSON/HTML with repetitive keys), where the win comes from LZ77.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compress src[0..len) into a freshly malloc'd gzip stream. On success
// returns 0 and hands the buffer to the caller via *out / *out_len (caller
// frees). On allocation failure returns -1 and *out is NULL.
int tulpar_gzip_compress(const unsigned char *src, size_t len,
                         unsigned char **out, size_t *out_len);

#ifdef __cplusplus
}
#endif
