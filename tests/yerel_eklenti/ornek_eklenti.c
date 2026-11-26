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
                                   (const void *)&k_son_kod};
