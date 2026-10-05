#!/usr/bin/env bash
# KULLANICI İKİLİSİ DİNAMİK BAĞ KAPISI — `tulpar build` ÇIKTISI yalnız hedef
# sistemin kendi kitaplıklarına mı bağlı?
#
# NİYE VAR (ölçüldü 2026-10-05, macOS CI): `tools/dinamik_bag_denetle.sh`
# yayınlanan `tulpar` ikilisini Homebrew'suz açılır yaptı (#462) — ama
# `tulpar build`'in ürettiği programlar HÂLÂ Homebrew openssl@3'e bağlanıyordu:
# link satırı `-L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto` taşıyor ve
# ld64 aynı dizinde .dylib'i .a'ya tercih ediyor. openssl@3 olmayan Mac'te ise
# link `ld: library 'ssl' not found` ile düşüyordu. "Sürücü taşınabilir" ile
# "sürücünün ÜRETTİĞİ taşınabilir" ayrı iddialar; bu betik ikincisini ölçer.
#
# Program TLS koduna GERÇEKTEN dokunur (`tls_init(sertifika, anahtar)`; ağ
# yok): 0 dönmesi TLS'nin derlenmediği/çalışmadığı demek — "temiz ama TLS'siz"
# ikili geçmesin. Sertifika `openssl req` ile yerinde üretilir (macOS'ta
# /usr/bin/openssl LibreSSL); openssl CLI yoksa görünür ATLANIR,
# TULPAR_TLS_ZORUNLU=1 ile atlama DÜŞME sayılır (CI).
#
# POZİTİF KONTROL (her koşumda): TULPAR_AOT_LINK_FLAGS ile geçici dizindeki bir
# paylaşımlı kitaplığa bağlanan ikiliyi kapı KIRMIZI görmeli — yoksa "temiz"
# hiçbir şey ölçmemiş olurdu.
#
#   tests/kullanici_ikili_bag.sh [tulpar_yolu]
set -uo pipefail

TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DENETLE="$ROOT/tools/dinamik_bag_denetle.sh"
[ -f "$DENETLE" ] || { echo "HATA: $DENETLE yok" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

# Sertifika + anahtar (ag yok; tls_init yalniz dosyadan okur).
if ! command -v openssl >/dev/null 2>&1; then
    if [ "${TULPAR_TLS_ZORUNLU:-0}" = "1" ]; then
        echo "DUSTU: openssl CLI yok, TULPAR_TLS_ZORUNLU=1 — sertifika uretilemedi" >&2; exit 1
    fi
    echo "ATLANDI: openssl CLI yok — sertifika uretilemedi (TULPAR_TLS_ZORUNLU=1 ile dusme sayilir)"
    exit 0
fi
if ! openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
        -keyout "$TMP/anahtar.pem" -out "$TMP/sertifika.pem" > "$TMP/openssl.log" 2>&1; then
    echo "HATA: openssl req basarisiz:" >&2; tail -5 "$TMP/openssl.log" >&2; exit 1
fi

cat > "$TMP/tls.tpr" <<T
int ctx = tls_init("$TMP/sertifika.pem", "$TMP/anahtar.pem");
if (ctx != 0) { print("tls:var"); } else { print("tls:yok"); }
T

# 1) derle + linkle (onbellek kapali: bayrak degisikligi eski ikiliyi gostermesin)
if ! TULPAR_AOT_NOCACHE=1 "$TUL" build "$TMP/tls.tpr" "$TMP/tls" > "$TMP/build.log" 2>&1 || [ ! -x "$TMP/tls" ]; then
    dustu "TLS kullanan program linklenemedi:"; tail -15 "$TMP/build.log" | sed 's/^/         /'
    echo -e "\033[0;31mKULLANICI IKILISI BAG KAPISI KIRMIZI\033[0m" >&2; exit 1
fi
gecti "tulpar build: TLS kullanan program linklendi ($(wc -c < "$TMP/tls" | tr -d ' ') bayt)"

# 2) calistir: TLS kodu gercekten icinde ve calisiyor mu
out=$("$TMP/tls" 2>&1 | tail -1)
if [ "$out" = "tls:var" ]; then gecti "tls_init sertifikayi yukledi (TLS kodu ikilide ve calisiyor)"
else dustu "program ciktisi '$out' (beklenen 'tls:var' — TLS derlenmemis ya da calismiyor)"; fi

# 3) dinamik baglar: yalniz sistem kitapliklari
if bash "$DENETLE" "$TMP/tls" > "$TMP/bag.log" 2>&1; then
    gecti "uretilen ikilinin dinamik baglari yalniz sistem"
    sed 's/^/         /' "$TMP/bag.log"
else
    dustu "uretilen ikili sistem disi bir kitapliga bagli:"; sed 's/^/         /' "$TMP/bag.log"
fi

# 3b) BOYUT OLCUMU (iddia degil, basilir): TLS'ye dokunmayan sade programin
#     boyutu ve baglari. macOS'ta OpenSSL artik statik (runtime arsivinin
#     icinde); bedeli burada gorunur. Linux'ta libssl paylasimli, --as-needed
#     sade programdan onu dusurur.
printf 'print("sade");\n' > "$TMP/sade.tpr"
if TULPAR_AOT_NOCACHE=1 "$TUL" build "$TMP/sade.tpr" "$TMP/sade" > "$TMP/sade.log" 2>&1 && [ "$("$TMP/sade" 2>&1)" = "sade" ]; then
    bash "$DENETLE" "$TMP/sade" > "$TMP/sade_bag.log" 2>&1 || dustu "sade program sistem disi bir kitapliga bagli"
    echo "  olcum  sade program $(wc -c < "$TMP/sade" | tr -d ' ') bayt, TLS programi $(wc -c < "$TMP/tls" | tr -d ' ') bayt"
    # macOS: ld64 -dead_strip'in kazanci yalniz OLCULUR (link satirina
    # eklenmedi — aot_pipeline.cpp'deki not: macOS bayragi gercek makinede
    # olculmeden eklenmez). Calisiyor mu da basilir.
    if [ "$(uname -s)" = Darwin ]; then
        if TULPAR_AOT_NOCACHE=1 TULPAR_AOT_LINK_FLAGS=-Wl,-dead_strip "$TUL" build "$TMP/sade.tpr" "$TMP/sade_ds" > /dev/null 2>&1; then
            echo "  olcum  -dead_strip ile sade program $(wc -c < "$TMP/sade_ds" | tr -d ' ') bayt, cikti: '$("$TMP/sade_ds" 2>&1 | tail -1)'"
        else
            echo "  olcum  -dead_strip ile link basarisiz"
        fi
    fi
else
    dustu "sade program derlenemedi/kosmadi"; tail -5 "$TMP/sade.log" | sed 's/^/         /'
fi

# 4) POZITIF KONTROL: kapi uretilen ikilinin baglarina gercekten bakiyor mu —
#    gecici dizindeki paylasimli kitapliga baglanan ikili KIRMIZI olmali.
cc_bin="${CC:-}"
if [ -z "$cc_bin" ]; then for c in cc clang gcc; do command -v "$c" >/dev/null 2>&1 && { cc_bin="$c"; break; }; done; fi
printf 'int deneme_f(void) { return 7; }\n' > "$TMP/deneme.c"
case "$(uname -s)" in
    Darwin)
        "$cc_bin" -dynamiclib -o "$TMP/libdeneme.dylib" -install_name "$TMP/libdeneme.dylib" "$TMP/deneme.c" || dustu "deneme dylib derlenemedi"
        ek="-L$TMP -ldeneme" ;;   # ld64 verilen her dylib'i LC_LOAD_DYLIB yapar
    *)
        "$cc_bin" -shared -fPIC -o "$TMP/libdeneme.so" "$TMP/deneme.c" || dustu "deneme .so derlenemedi"
        # Linux link satiri --as-needed tasiyor; kullanilmayan kitaplik dusmesin.
        ek="-Wl,--no-as-needed -L$TMP -ldeneme -Wl,-rpath,$TMP" ;;
esac
if TULPAR_AOT_NOCACHE=1 TULPAR_AOT_LINK_FLAGS="$ek" "$TUL" build "$TMP/tls.tpr" "$TMP/tls_kirli" > "$TMP/kirli.log" 2>&1 && [ -x "$TMP/tls_kirli" ]; then
    bash "$DENETLE" "$TMP/tls_kirli" > "$TMP/kirli_bag.log" 2>&1; rc=$?
    if [ $rc -eq 1 ] && grep -q "deneme" "$TMP/kirli_bag.log"; then
        gecti "pozitif kontrol: gecici dizindeki kitapliga bagli ikili KIRMIZI"
    else
        dustu "pozitif kontrol: sistem disi bag yakalanmadi (rc=$rc)"; sed 's/^/         /' "$TMP/kirli_bag.log"
    fi
else
    dustu "pozitif kontrol kurulamadi (TULPAR_AOT_LINK_FLAGS ile link basarisiz):"; tail -8 "$TMP/kirli.log" | sed 's/^/         /'
fi

if [ "$fail" -eq 0 ]; then
    echo -e "\033[0;32mkullanici ikilisi bag kapisi temiz\033[0m (TLS programi linklendi, calisti, yalniz sistem kitapliklari; pozitif kontrol kirmizi)"
else
    echo -e "\033[0;31mKULLANICI IKILISI BAG KAPISI KIRMIZI\033[0m" >&2
fi
exit "$fail"
