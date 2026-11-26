/* ORNEK YEREL EKLENTI (K303) — tests/yerel_eklenti.sh bunu statik arsive
 * derleyip `--ext` ile Tulpar'a baglar.
 *
 * Bildirim (tulpar-ext.json) her fonksiyonun C tipini soyler; derleyici
 * cagriyi o tiplerle DOGRUDAN kurar. Derleyici C tarafini goremez (statik
 * arsivde tip bilgisi yok), o yuzden uyumu eklenti KENDI derlemesinde
 * kilitler: asagidaki ABI_KILIDI blogu bildirimdeki her imzayi bir isaretci
 * tipine atar — imza kayarsa bu dosya DERLENMEZ (-Werror=incompatible-
 * pointer-types; testin kendi pozitif kontrolu bunu bir kez bozup sinar).
 *
 * Her tip ailesi en az bir kez: i64, i32, f64, f32, bool (C int), str (giris
 * ve donus, NULL donus), void, 10 parametre (yazmaclar tukenip yigina tasan
 * karisik int/double), ve Tulpar'i GERI cagiran duz C yuzu
 * (tulpar_ext_func_lookup / tulpar_ext_call_f64 — runtime_bindings.cpp). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Tulpar runtime'in eklentilere actigi duz C yuzu (VMValue gormez). */
void *tulpar_ext_func_lookup(const char *name, int *arity);
int tulpar_ext_call_f64(void *fn, int arity, const double *args, int argc);   /* sonuc atilir */
double tulpar_ext_eval_f64(void *fn, int arity, const double *args, int argc); /* sonuc double */
int tulpar_ext_threw(void); /* son eval throw ile mi bitti (1/0) */

int64_t ornek_topla(int64_t a, int64_t b) { return a + b; }
int32_t ornek_kare32(int32_t x) { return x * x; }
double ornek_orta(double a, double b) { return (a + b) * 0.5; }
float ornek_yarim(float x) { return x * 0.5f; }
int ornek_pozitif_mi(double x) { return x > 0.0; }
int ornek_bayrak(int b) { return b ? 7 : 3; }

static char g_tampon[128];
/* Statik tampon: derleyici donen dizgiyi HEMEN kopyalamali — bir sonraki
 * cagri ayni tamponu ezer (test ikisini yan yana okuyup sinar). */
const char *ornek_selam(const char *ad) {
  snprintf(g_tampon, sizeof g_tampon, "Merhaba, %s!", ad ? ad : "(null)");
  return g_tampon;
}
int64_t ornek_uzunluk(const char *s) { return s ? (int64_t)strlen(s) : -1; }
const char *ornek_bos(void) { return NULL; }

static int64_t g_sayac = 0;
void ornek_sayac_artir(void) { g_sayac++; }
int64_t ornek_sayac(void) { return g_sayac; }

/* 10 parametre, karisik: SysV'de 6 tamsayi + 8 kayan yazmac; burada 5 + 5,
 * ama Win64'te 4 yazmac TOPLAM — yani 5.'den itibaren yigin. */
double ornek_on(int64_t a, double b, int64_t c, double d, int32_t e, double f, int64_t g,
                double h, int32_t i, double j) {
  return (double)a + b * 2 + (double)c * 3 + d * 4 + (double)e * 5 + f * 6 + (double)g * 7 +
         h * 8 + (double)i * 9 + j * 10;
}

/* Tulpar fonksiyonunu ADIYLA cozup cagir (motorun betik kancalarinin
 * kalibi: yuklemede coz, sonra isaretciyle cagir). Bulunamazsa -1; sonucsuz
 * cagri da (tulpar_ext_call_f64) bir kez sinanir: 1 donmeli. */
double ornek_geri_cagir(const char *fn, double x) {
  int arity = -1;
  void *p = tulpar_ext_func_lookup(fn, &arity);
  if (!p) return -1.0;
  if (tulpar_ext_call_f64(p, arity, &x, 1) != 1) return -2.0;
  return tulpar_ext_eval_f64(p, arity, &x, 1);
}

/* EXT SINIRI (RFC C2, 2026-11-26): "kilit al, Tulpar'i cagir, kilidi birak".
 * Geri cagrilan fonksiyon throw ederse runtime onu SINIRDA yakalar: burada
 * hata kodu doner, temizlik (g_kilit = 0) KOSAR, istisna bu fonksiyon
 * Tulpar'a donunce yeniden firlar. Eskiden throw, disarida bir Tulpar try'i
 * varsa bu cercevenin USTUNDEN longjmp yapiyordu: kilit takili kaliyor, kod
 * hic yazilmiyordu (olculdu: kilit 1, kod 99). */
static int64_t g_kilit = 0;
static int64_t g_son_kod = 99;
int64_t ornek_koru(const char *fn, double x) {
  int ar = -1;
  void *p = tulpar_ext_func_lookup(fn, &ar);
  g_kilit = 1;
  g_son_kod = tulpar_ext_call_f64(p, ar, &x, 1);
  g_kilit = 0;
  return g_son_kod;
}
/* eval yolu: throw -> 0.0, ayirt etmek icin tulpar_ext_threw(). */
double ornek_koru_eval(const char *fn, double x) {
  int ar = -1;
  void *p = tulpar_ext_func_lookup(fn, &ar);
  g_kilit = 1;
  double r = tulpar_ext_eval_f64(p, ar, &x, 1);
  g_son_kod = tulpar_ext_threw() ? -1 : 1;
  g_kilit = 0;
  return r;
}
/* Iki kanca art arda, ikisi de atabilir: kod = rc1 * 10 + rc2. Tulpar'a ILK
 * istisna firlar. */
int64_t ornek_iki_kanca(const char *a, const char *b, double x) {
  int ar1 = -1, ar2 = -1;
  void *p1 = tulpar_ext_func_lookup(a, &ar1);
  void *p2 = tulpar_ext_func_lookup(b, &ar2);
  g_kilit = 1;
  int r1 = tulpar_ext_call_f64(p1, ar1, &x, 1);
  int r2 = tulpar_ext_call_f64(p2, ar2, &x, 1);
  g_son_kod = (int64_t)r1 * 10 + r2;
  g_kilit = 0;
  return g_son_kod;
}
int64_t ornek_kilit(void) { return g_kilit; }
int64_t ornek_son_kod(void) { return g_son_kod; }

/* Calisan programin gordugu TULPAR_EXT_PATH (yoksa NULL -> Tulpar'da "").
 * Bir eklentinin kendi kaynaklarini (font, doku) calisma aninda nasil
 * buldugunun kalibi: `tulpar <dosya>` eklenti dizinlerini oraya yazar. */
#include <stdlib.h>
const char *ornek_ext_yolu(void) { return getenv("TULPAR_EXT_PATH"); }

/* ---- FFI v1 dilimi (2026-11-26): opak `ptr` + gercek C geri cagrisi ----
 * Tampon C'de yasar; Tulpar yalniz isaretciyi tutar ve geri verir. Siralama
 * GERCEK libc qsort ile: Tulpar karsilastiricisi derleyicinin urettigi C-ABI
 * trampolininden gecer — C kodu Tulpar'a ozgu hicbir sey bilmez. */
#include <stdlib.h>
void *ornek_tampon_yeni(int64_t n) { return calloc((size_t)n, sizeof(int64_t)); }
void ornek_tampon_yaz(void *p, int64_t i, int64_t v) { ((int64_t *)p)[i] = v; }
int64_t ornek_tampon_oku(void *p, int64_t i) { return ((int64_t *)p)[i]; }
int64_t ornek_oku_i64(void *p) { return *(int64_t *)p; }
void ornek_tampon_sil(void *p) { free(p); }
int ornek_null_mu(void *p) { return p == NULL; }
void ornek_qsort(void *base, int64_t n, int64_t boy, int (*cmp)(const void *, const void *)) {
  qsort(base, (size_t)n, (size_t)boy, cmp);
}
/* Baglam isaretcili geri cagri: cb(i, ctx) n kez. */
void ornek_her_biri(int64_t n, void (*cb)(int64_t, void *), void *ctx) {
  for (int64_t i = 0; i < n; i++) cb(i, ctx);
}
double ornek_uygula_f(double (*f)(double), double x) { return f(x) + f(x * 2.0); }
int64_t ornek_uygula_s(int64_t (*f)(const char *), const char *s) { return f(s) * 10; }
int32_t ornek_uygula_32(int32_t (*f)(int32_t, float, int), int32_t a) { return f ? f(a, 0.5f, 1) : -1; }
/* C2 siniri geri cagrida: kilit al, cagir, birak. */
int64_t ornek_cb_koru(int64_t (*f)(int64_t), int64_t x) {
  g_kilit = 1;
  int64_t r = f(x);
  g_kilit = 0;
  g_son_kod = r;
  return r;
}

/* ---- K304: struct DEGER gecisi (2026-11-26) — ornek.tpr'deki @repr(C) ikizleri ---- */
#include <stdbool.h>
typedef struct { int32_t a, b; } Ikii;            /* 8 bayt: INTEGER        */
typedef struct { double x, y; } Ikid;             /* 16: SSE+SSE / HFA2     */
typedef struct { float a, b, c, d; } Dortf;       /* 16: <2xf>x2 / HFA4     */
typedef struct { int32_t a; float b; int64_t c; } Karisik; /* 16: INT+INT  */
typedef struct { double x, y, z; } Ucd;           /* 24: byval / sret       */
typedef struct { bool a; int32_t n; } Kucuk;      /* 8: INTEGER (bool 1B)   */
typedef struct { double x; } Tekd;                /* 8: SSE                 */
Ikii ornek_st_topla(Ikii p, Ikii q) { Ikii r = {p.a + q.a, p.b + q.b}; return r; }
Ikid ornek_st_olcek(Ikid v, double k) { Ikid r = {v.x * k, v.y * k}; return r; }
float ornek_st_dort_toplam(Dortf v) { return v.a + v.b * 10 + v.c * 100 + v.d * 1000; }
Dortf ornek_st_dort_ters(Dortf v) { Dortf r = {v.d, v.c, v.b, v.a}; return r; }
Karisik ornek_st_karisik(Karisik k) { Karisik r = {k.a + 1, k.b * 2, k.c - 1}; return r; }
Ucd ornek_st_capraz(Ucd a, Ucd b) {
  Ucd r = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  return r;
}
int32_t ornek_st_kucuk(Kucuk k) { return k.a ? k.n : -k.n; }
Kucuk ornek_st_kucuk_yap(int32_t n) { Kucuk r = {n > 0, n * 2}; return r; }
Tekd ornek_st_tek(Tekd t) { Tekd r = {t.x + 0.5}; return r; }
/* Karisik arguman listesi: yazmac ayirma struct parcalariyla dogru mu. */
double ornek_st_cok(int64_t x, Ucd a, double y, Ikid b, int32_t z, Karisik k) {
  return (double)x + a.x + a.y + a.z + y + b.x * b.y + z + k.a + k.b + (double)k.c;
}
/* C struct'i degistirir: Tulpar kopyasi etkilenmemeli (Ucd dolayli gecer). */
double ornek_st_boz(Ucd a) { a.x = -999; return a.y; }

/* ---- ABI_KILIDI: bildirimdeki imzalar (tulpar-ext.json ile AYNI) ---- */
#ifndef ORNEK_ABI_BOZ
typedef int64_t (*abi_topla)(int64_t, int64_t);
#else
typedef int64_t (*abi_topla)(int64_t, double); /* pozitif kontrol: kasitli kayma */
#endif
static const abi_topla k_topla = ornek_topla;
static int32_t (*const k_kare32)(int32_t) = ornek_kare32;
static double (*const k_orta)(double, double) = ornek_orta;
static float (*const k_yarim)(float) = ornek_yarim;
static int (*const k_pozitif)(double) = ornek_pozitif_mi;
static int (*const k_bayrak)(int) = ornek_bayrak;
static const char *(*const k_selam)(const char *) = ornek_selam;
static int64_t (*const k_uzunluk)(const char *) = ornek_uzunluk;
static const char *(*const k_bos)(void) = ornek_bos;
static void (*const k_artir)(void) = ornek_sayac_artir;
static int64_t (*const k_sayac)(void) = ornek_sayac;
static double (*const k_on)(int64_t, double, int64_t, double, int32_t, double, int64_t, double,
                            int32_t, double) = ornek_on;
static double (*const k_geri)(const char *, double) = ornek_geri_cagir;
static const char *(*const k_ext_yolu)(void) = ornek_ext_yolu;
static int64_t (*const k_koru)(const char *, double) = ornek_koru;
static double (*const k_koru_eval)(const char *, double) = ornek_koru_eval;
static int64_t (*const k_iki)(const char *, const char *, double) = ornek_iki_kanca;
static int64_t (*const k_kilit)(void) = ornek_kilit;
static int64_t (*const k_son_kod)(void) = ornek_son_kod;
static void *(*const k_tampon_yeni)(int64_t) = ornek_tampon_yeni;
static void (*const k_tampon_yaz)(void *, int64_t, int64_t) = ornek_tampon_yaz;
static int64_t (*const k_tampon_oku)(void *, int64_t) = ornek_tampon_oku;
static int64_t (*const k_oku_i64)(void *) = ornek_oku_i64;
static void (*const k_tampon_sil)(void *) = ornek_tampon_sil;
static int (*const k_null_mu)(void *) = ornek_null_mu;
static void (*const k_qsort)(void *, int64_t, int64_t, int (*)(const void *, const void *)) = ornek_qsort;
static void (*const k_her_biri)(int64_t, void (*)(int64_t, void *), void *) = ornek_her_biri;
static double (*const k_uygula_f)(double (*)(double), double) = ornek_uygula_f;
static int64_t (*const k_uygula_s)(int64_t (*)(const char *), const char *) = ornek_uygula_s;
static int32_t (*const k_uygula_32)(int32_t (*)(int32_t, float, int), int32_t) = ornek_uygula_32;
static int64_t (*const k_cb_koru)(int64_t (*)(int64_t), int64_t) = ornek_cb_koru;
static Ikii (*const k_st1)(Ikii, Ikii) = ornek_st_topla;
static Ikid (*const k_st2)(Ikid, double) = ornek_st_olcek;
static float (*const k_st3)(Dortf) = ornek_st_dort_toplam;
static Dortf (*const k_st4)(Dortf) = ornek_st_dort_ters;
static Karisik (*const k_st5)(Karisik) = ornek_st_karisik;
static Ucd (*const k_st6)(Ucd, Ucd) = ornek_st_capraz;
static int32_t (*const k_st7)(Kucuk) = ornek_st_kucuk;
static Kucuk (*const k_st8)(int32_t) = ornek_st_kucuk_yap;
static Tekd (*const k_st9)(Tekd) = ornek_st_tek;
static double (*const k_st10)(int64_t, Ucd, double, Ikid, int32_t, Karisik) = ornek_st_cok;
static double (*const k_st11)(Ucd) = ornek_st_boz;
/* Kullanilmayan-degisken uyarisi olmasin diye tek bir tutucu. */
const void *ornek_abi_kilidi[] = {(const void *)&k_topla, (const void *)&k_kare32,
                                   (const void *)&k_orta, (const void *)&k_yarim,
                                   (const void *)&k_pozitif, (const void *)&k_bayrak,
                                   (const void *)&k_selam, (const void *)&k_uzunluk,
                                   (const void *)&k_bos, (const void *)&k_artir,
                                   (const void *)&k_sayac, (const void *)&k_on,
                                   (const void *)&k_geri, (const void *)&k_ext_yolu,
                                   (const void *)&k_koru, (const void *)&k_koru_eval,
                                   (const void *)&k_iki, (const void *)&k_kilit,
                                   (const void *)&k_son_kod, (const void *)&k_tampon_yeni,
                                   (const void *)&k_tampon_yaz, (const void *)&k_tampon_oku,
                                   (const void *)&k_oku_i64, (const void *)&k_tampon_sil,
                                   (const void *)&k_null_mu, (const void *)&k_qsort,
                                   (const void *)&k_her_biri, (const void *)&k_uygula_f,
                                   (const void *)&k_uygula_s, (const void *)&k_uygula_32,
                                   (const void *)&k_cb_koru, (const void *)&k_st1,
                                   (const void *)&k_st2, (const void *)&k_st3, (const void *)&k_st4,
                                   (const void *)&k_st5, (const void *)&k_st6, (const void *)&k_st7,
                                   (const void *)&k_st8, (const void *)&k_st9, (const void *)&k_st10,
                                   (const void *)&k_st11};
