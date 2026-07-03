// In-tree gzip compressor: DEFLATE fixed-Huffman blocks (RFC 1951 §3.2.6)
// wrapped in the gzip container (RFC 1952). See tulpar_gzip.h for scope.
#include "tulpar_gzip.h"

#include <stdlib.h>
#include <string.h>

// ---- CRC-32 (IEEE 802.3, reflected poly 0xEDB88320) -------------------------
// Lazy table init is idempotent (every thread writes identical values), same
// accepted pattern as the runtime's base64 decode table.
static unsigned int g_crc_table[256];
static int g_crc_ready = 0;

static void crc_init(void) {
  for (unsigned int i = 0; i < 256; i++) {
    unsigned int c = i;
    for (int k = 0; k < 8; k++)
      c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    g_crc_table[i] = c;
  }
  g_crc_ready = 1;
}

static unsigned int crc32_of(const unsigned char *p, size_t n) {
  if (!g_crc_ready)
    crc_init();
  unsigned int c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++)
    c = g_crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

// ---- LSB-first bit writer over a growable buffer -----------------------------
typedef struct {
  unsigned char *buf;
  size_t cap, len;
  unsigned int bitbuf;
  int bitcnt;
  int oom;
} BitW;

static int bw_grow(BitW *w, size_t need) {
  if (w->oom)
    return -1;
  if (w->len + need <= w->cap)
    return 0;
  size_t ncap = w->cap ? w->cap * 2 : 1024;
  while (ncap < w->len + need)
    ncap *= 2;
  unsigned char *nb = (unsigned char *)realloc(w->buf, ncap);
  if (!nb) {
    w->oom = 1;
    return -1;
  }
  w->buf = nb;
  w->cap = ncap;
  return 0;
}

static void bw_byte(BitW *w, unsigned char b) {
  if (bw_grow(w, 1) != 0)
    return;
  w->buf[w->len++] = b;
}

// Append `n` bits of `val`, LSB first (DEFLATE data-element order).
static void bw_bits(BitW *w, unsigned int val, int n) {
  w->bitbuf |= (val & ((1u << n) - 1u)) << w->bitcnt;
  w->bitcnt += n;
  while (w->bitcnt >= 8) {
    bw_byte(w, (unsigned char)(w->bitbuf & 0xFFu));
    w->bitbuf >>= 8;
    w->bitcnt -= 8;
  }
}

// Append an n-bit Huffman code. Huffman codes are packed starting from the
// code's MSB (RFC 1951 §3.1.1), so reverse the bits before the LSB-first
// writer sees them.
static void bw_huff(BitW *w, unsigned int code, int n) {
  unsigned int r = 0;
  for (int i = 0; i < n; i++)
    r |= ((code >> (n - 1 - i)) & 1u) << i;
  bw_bits(w, r, n);
}

static void bw_flush(BitW *w) {
  if (w->bitcnt > 0) {
    bw_byte(w, (unsigned char)(w->bitbuf & 0xFFu));
    w->bitbuf = 0;
    w->bitcnt = 0;
  }
}

// ---- Fixed-Huffman symbol emitters -------------------------------------------
// Literal/length alphabet (RFC 1951 §3.2.6): 0-143 → 8 bits from 0x30,
// 144-255 → 9 bits from 0x190, 256-279 → 7 bits from 0, 280-287 → 8 bits
// from 0xC0.
static void emit_litlen(BitW *w, int sym) {
  if (sym <= 143)
    bw_huff(w, 0x30u + (unsigned int)sym, 8);
  else if (sym <= 255)
    bw_huff(w, 0x190u + (unsigned int)(sym - 144), 9);
  else if (sym <= 279)
    bw_huff(w, (unsigned int)(sym - 256), 7);
  else
    bw_huff(w, 0xC0u + (unsigned int)(sym - 280), 8);
}

// Length code table: base length + extra bits per code 257..285 (§3.2.5).
static const unsigned short LEN_BASE[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const unsigned char LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                            1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                            4, 4, 4, 4, 5, 5, 5, 5, 0};

// Distance code table: base distance + extra bits per code 0..29.
static const unsigned int DIST_BASE[30] = {
    1,    2,    3,    4,    5,    7,     9,     13,    17,    25,
    33,   49,   65,   97,   129,  193,   257,   385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
static const unsigned char DIST_EXTRA[30] = {0, 0, 0,  0,  1,  1,  2,  2,
                                             3, 3, 4,  4,  5,  5,  6,  6,
                                             7, 7, 8,  8,  9,  9,  10, 10,
                                             11, 11, 12, 12, 13, 13};

static void emit_match(BitW *w, int len, int dist) {
  int lc = 28;
  while (lc > 0 && LEN_BASE[lc] > (unsigned)len)
    lc--;
  // Codes 264/285 boundaries: LEN_BASE is ascending, the scan lands on the
  // greatest base <= len, which is exactly the right code.
  emit_litlen(w, 257 + lc);
  if (LEN_EXTRA[lc])
    bw_bits(w, (unsigned int)len - LEN_BASE[lc], LEN_EXTRA[lc]);

  int dc = 29;
  while (dc > 0 && DIST_BASE[dc] > (unsigned)dist)
    dc--;
  bw_huff(w, (unsigned int)dc, 5); // fixed 5-bit distance codes
  if (DIST_EXTRA[dc])
    bw_bits(w, (unsigned int)dist - DIST_BASE[dc], DIST_EXTRA[dc]);
}

// ---- Greedy LZ77 over a 32K window --------------------------------------------
#define GZ_WSIZE 32768
#define GZ_WMASK (GZ_WSIZE - 1)
#define GZ_HBITS 15
#define GZ_HSIZE (1 << GZ_HBITS)
#define GZ_HMASK (GZ_HSIZE - 1)
#define GZ_MIN_MATCH 3
#define GZ_MAX_MATCH 258
#define GZ_MAX_CHAIN 128

static unsigned int gz_hash(const unsigned char *p) {
  return (((unsigned int)p[0] << 10) ^ ((unsigned int)p[1] << 5) ^
          (unsigned int)p[2]) &
         GZ_HMASK;
}

int tulpar_gzip_compress(const unsigned char *src, size_t len,
                         unsigned char **out, size_t *out_len) {
  *out = NULL;
  *out_len = 0;

  BitW w;
  memset(&w, 0, sizeof(w));

  // gzip header: magic, CM=8 (deflate), no flags, mtime 0, XFL 0, OS 3 (unix).
  const unsigned char hdr[10] = {0x1f, 0x8b, 0x08, 0, 0, 0, 0, 0, 0, 3};
  for (int i = 0; i < 10; i++)
    bw_byte(&w, hdr[i]);

  // Single DEFLATE block: BFINAL=1, BTYPE=01 (fixed Huffman).
  bw_bits(&w, 1, 1);
  bw_bits(&w, 1, 2);

  long *head = (long *)malloc(sizeof(long) * GZ_HSIZE);
  long *prev = (long *)malloc(sizeof(long) * GZ_WSIZE);
  if (!head || !prev) {
    free(head);
    free(prev);
    free(w.buf);
    return -1;
  }
  for (int i = 0; i < GZ_HSIZE; i++)
    head[i] = -1;
  for (int i = 0; i < GZ_WSIZE; i++)
    prev[i] = -1;

  size_t pos = 0;
  while (pos < len) {
    int best_len = 0;
    long best_dist = 0;
    if (pos + GZ_MIN_MATCH <= len) {
      unsigned int h = gz_hash(src + pos);
      long cand = head[h];
      int chain = GZ_MAX_CHAIN;
      size_t limit = len - pos;
      if (limit > GZ_MAX_MATCH)
        limit = GZ_MAX_MATCH;
      while (cand >= 0 && chain-- > 0 && pos - (size_t)cand <= GZ_WSIZE) {
        const unsigned char *a = src + pos;
        const unsigned char *b = src + cand;
        size_t m = 0;
        while (m < limit && a[m] == b[m])
          m++;
        if ((int)m > best_len) {
          best_len = (int)m;
          best_dist = (long)(pos - (size_t)cand);
          if (m >= limit)
            break;
        }
        long next = prev[cand & GZ_WMASK];
        if (next >= cand)
          break; // stale wrap-around entry: chains must strictly descend
        cand = next;
      }
      // Insert the current position into the hash chain.
      prev[pos & GZ_WMASK] = head[h];
      head[h] = (long)pos;
    }

    if (best_len >= GZ_MIN_MATCH && best_dist >= 1) {
      emit_match(&w, best_len, (int)best_dist);
      // Register the skipped positions so later matches can point at them.
      size_t end = pos + (size_t)best_len;
      size_t ins = pos + 1;
      while (ins < end && ins + GZ_MIN_MATCH <= len) {
        unsigned int h2 = gz_hash(src + ins);
        prev[ins & GZ_WMASK] = head[h2];
        head[h2] = (long)ins;
        ins++;
      }
      pos = end;
    } else {
      emit_litlen(&w, src[pos]);
      pos++;
    }
  }

  emit_litlen(&w, 256); // end-of-block
  bw_flush(&w);

  // gzip trailer: CRC-32 then ISIZE, both little-endian.
  unsigned int crc = crc32_of(src, len);
  unsigned int isz = (unsigned int)(len & 0xFFFFFFFFu);
  for (int i = 0; i < 4; i++)
    bw_byte(&w, (unsigned char)((crc >> (8 * i)) & 0xFFu));
  for (int i = 0; i < 4; i++)
    bw_byte(&w, (unsigned char)((isz >> (8 * i)) & 0xFFu));

  free(head);
  free(prev);
  if (w.oom) {
    free(w.buf);
    return -1;
  }
  *out = w.buf;
  *out_len = w.len;
  return 0;
}
