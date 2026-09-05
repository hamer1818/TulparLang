/* Runtime giris noktalarini DOGRUDAN cagiran ASAN kosum takimi.
 *
 * NEDEN VAR: Tulpar paketleri (`*.test.tpr`) yalniz GORUNUR davranisi
 * olcuyor. Bir runtime fonksiyonu tamponun disina yazarsa, yazilan baytlar
 * genelde malloc'un bos payina dusuyor ve test YESIL geciyor. Iki kez
 * yasandi (2026-09-05):
 *   * sekil onbelleginde sarkan `idata` (yalniz 4096 elemanda gorunur oldu)
 *   * sb_append(int) yer ayirmasini 24 -> 4 bayta dusuren enjeksiyon
 *     HICBIR paketi kirmadi.
 * ASAN bu sinifi deterministik yakaliyor.
 *
 * Kur ve kos:  tests/run_asan.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct { char *buffer; int length; int capacity; } StringBuilder;

/* Runtime C++ ile derleniyor ama semboller `extern "C"`; bu dosya c++ ile
 * derlendiginde ad bozma olmasin diye ayni bildirimi kullaniyoruz. */
#ifdef __cplusplus
extern "C" {
#endif
StringBuilder *aot_stringbuilder_new(int initial_capacity);
void aot_stringbuilder_append(StringBuilder *sb, const char *s, int len);
void aot_stringbuilder_append_int(StringBuilder *sb, long long v);
void aot_stringbuilder_free(StringBuilder *sb);
#ifdef __cplusplus
}
#endif

static int fails = 0;
static void check(int cond, const char *what) {
    if (!cond) { printf("  x %s\n", what); fails++; }
}

/* StringBuilder: int ekleme rakamlari DOGRUDAN tampona yaziyor, yani yer
 * ayirma hesabi tasmaya karsi tek koruma. Tampon DOLMAYA yaklasirken
 * eklemek kritik durum: kapasite 64'ten baslayip 20 haneli sayilarla
 * doldurulunca buyume esigi tam sinirda test ediliyor. */
static void t_sb_int_overflow(void) {
    for (int start = 0; start < 40; start++) {
        StringBuilder *sb = aot_stringbuilder_new(1);   /* en az 64'e yuvarlaniyor */
        for (int i = 0; i < start; i++) aot_stringbuilder_append(sb, "x", 1);
        aot_stringbuilder_append_int(sb, INT64_MIN);    /* 20 karakter + NUL */
        aot_stringbuilder_append_int(sb, INT64_MAX);
        check((int)strlen(sb->buffer) == sb->length, "sb uzunluk == strlen");
        check(sb->length <= sb->capacity, "sb length <= capacity");
        aot_stringbuilder_free(sb);
    }
}

/* Cok sayida ekleme: her buyume adiminda tasma olur mu? */
static void t_sb_many(void) {
    StringBuilder *sb = aot_stringbuilder_new(1);
    long long expect = 0;
    for (long long i = 0; i < 5000; i++) {
        aot_stringbuilder_append_int(sb, i);
        char tmp[24]; expect += snprintf(tmp, sizeof tmp, "%lld", i);
    }
    check(sb->length == (int)expect, "sb toplam uzunluk");
    check((int)strlen(sb->buffer) == sb->length, "sb NUL sonlu");
    aot_stringbuilder_free(sb);
}

/* Dizgi ekleme yolu (int yolundan ayri) bozulmamis mi? */
static void t_sb_str(void) {
    StringBuilder *sb = aot_stringbuilder_new(1);
    for (int i = 0; i < 5000; i++) aot_stringbuilder_append(sb, "abcde", 5);
    check(sb->length == 25000, "sb dizgi uzunlugu");
    check((int)strlen(sb->buffer) == 25000, "sb dizgi NUL sonlu");
    aot_stringbuilder_free(sb);
}

int main(void) {
    printf("runtime ASAN kosum takimi\n");
    t_sb_int_overflow();
    t_sb_many();
    t_sb_str();
    if (fails) { printf("%d sorun\n", fails); return 1; }
    printf("hepsi gecti\n");
    return 0;
}
