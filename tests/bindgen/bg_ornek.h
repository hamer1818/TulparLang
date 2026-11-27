/* tests/bindgen.sh fiksturu — bindgen'in tanimasi gereken bildirim bicimleri.
 * Her blok bir C basliginda gercekte gorulen bir kalip. */
#ifndef BG_ORNEK_H
#define BG_ORNEK_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "bg_tipler.h"   /* tipler baska dosyada (SDL_stdinc.h kalibi) */

#ifdef __cplusplus
extern "C" {
#endif

#define BG_API __attribute__((visibility("default")))
#define BGCALL
#define bg_uzun long long   /* nesne-bicimli makro tip (zlib z_off_t kalibi); LP64 = LLP64 */

typedef struct BgTutamac BgTutamac;            /* opak tutamac */
typedef enum { BG_KIRMIZI, BG_YESIL = 5 } BgRenk;
typedef int (BGCALL *BgKarsilastir)(const void *a, const void *b);
typedef struct { int x, y; } BgNokta;          /* deger gecisi: atlanmali */
typedef bg_u32 BgKimlik, *BgKimlikP;           /* virgullu typedef, zincir */

BG_API int64_t BGCALL bg_topla(int64_t a, int64_t b);
BG_API double bg_olcek(double x, float k);
BG_API bg_u32 bg_kimlik(BgKimlik k);
BG_API bg_uzun bg_uzun_iki(bg_uzun x);
BG_API const char *bg_ad(void);
BG_API size_t bg_uzunluk(const char *s);
BG_API BgTutamac *bg_ac(int boy);
BG_API void bg_yaz(BgTutamac *t, int i, int v);
BG_API int bg_oku(BgTutamac *t, int i);
BG_API void bg_kapat(BgTutamac *t);
BG_API void bg_sirala(BgTutamac *t, BgKarsilastir cmp);
BG_API int bg_her_biri(BgTutamac *t, void (*cb)(int i, int v, void *ctx), void *ctx);
BG_API bool bg_cift_mi(int x);
BG_API bool bg_degil(bool b);
BG_API BgRenk bg_renk(enum BgRenkE r);
BG_API int bg_dizi_topla(const int dizi[], int n);
BG_API int bg_int_oku(const void *p);
/* K304: struct DEGER olarak */
struct BgEtiket { double u; double v; };
typedef struct BgEtiket BgEtiketT;
BG_API BgNokta bg_nokta_kaydir(BgNokta p, int dx);
BG_API double bg_etiket(struct BgEtiket e, BgEtiketT f);
typedef struct { short a; int b; } BgKisa;

/* atlanmasi gerekenler */
BG_API int bg_yazdir(const char *fmt, ...);
BG_API BgNokta bg_nokta(int x, int y);
BG_API int bg_kisa(BgKisa k);
BG_API long double bg_hassas(void);
static inline int bg_satir_ici(int x) { return x + 1; }
BG_API int (*bg_isaretci_donduren(void))(int);

#ifdef __cplusplus
}
#endif
#endif
