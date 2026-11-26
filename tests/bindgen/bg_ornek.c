#include "bg_ornek.h"
#include <stdlib.h>
#include <string.h>
struct BgTutamac { int n; int *d; };
int64_t bg_topla(int64_t a, int64_t b) { return a + b; }
double bg_olcek(double x, float k) { return x * k; }
bg_u32 bg_kimlik(BgKimlik k) { return k + 1; }
long bg_uzun_iki(long x) { return x * 2; }
const char *bg_ad(void) { return "bindgen"; }
size_t bg_uzunluk(const char *s) { return strlen(s); }
BgTutamac *bg_ac(int boy) { BgTutamac *t = malloc(sizeof *t); t->n = boy; t->d = calloc((size_t)boy, sizeof(int)); return t; }
void bg_yaz(BgTutamac *t, int i, int v) { t->d[i] = v; }
int bg_oku(BgTutamac *t, int i) { return t->d[i]; }
void bg_kapat(BgTutamac *t) { free(t->d); free(t); }
void bg_sirala(BgTutamac *t, BgKarsilastir cmp) { qsort(t->d, (size_t)t->n, sizeof(int), cmp); }
int bg_her_biri(BgTutamac *t, void (*cb)(int, int, void *), void *ctx) {
  for (int i = 0; i < t->n; i++) cb(i, t->d[i], ctx);
  return t->n;
}
/* Ust bitleri BILEREK kirli birak: cbool'u int gibi okuyan cagiran yanilir. */
bool bg_cift_mi(int x) { return (x % 2) == 0; }
bool bg_degil(bool b) { return !b; }
BgRenk bg_renk(enum BgRenkE r) { return r == BG_MAVI ? BG_YESIL : BG_KIRMIZI; }
int bg_dizi_topla(const int dizi[], int n) { int s = 0; for (int i = 0; i < n; i++) s += dizi[i]; return s; }
int bg_yazdir(const char *fmt, ...) { (void)fmt; return 0; }
int bg_int_oku(const void *p) { return *(const int *)p; }
