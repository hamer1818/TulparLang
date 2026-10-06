#!/usr/bin/env bash
# BOLUMLU (PARALEL) NESNE URETIMI KAPISI (src/aot/llvm_bolum.cpp).
#
# NIYE: nesne uretimi tek is parcaciginda kosuyordu ve orta boy programda
# derlemenin ucte biriydi (olculdu 2026-10-05, Ryzen 7 9800X3D:
# wings_groups_test emit-obj 220 ms / 627 ms). Bolumlu uretim kodu ancak
# CALISMA HIZINA DOKUNMADAN derleme suresini kisaltiyorsa kabul edilebilir.
# Fonksiyon yerlesimi bir bayt kaysa "kod ayni, program %2 yavas" sinifina
# (Tuzaklar 7i, hizalama) dusulur. Bu kapi uc seyi olcer:
#
#   1. GERCEKTEN BOLUNUYOR: buyuk programda [AOT-BOLUM] satiri, bolum >= 4
#      ve en buyuk bolumun komut payi <= %35 (BELIRLENIMLI olcu — kod
#      uretiminin paralel kritik yolu; duvar saati CI'da gurultulu).
#   2. AYNI IKILI: TULPAR_AOT_BOLUM=1 (eski tek nesne yolu) ile otomatik
#      bolumlu derlemenin ikilisinde her fonksiyon AYNI adreste; .text'in
#      NOP dolgusu disindaki komut akisi ve .rodata/.data baytlari AYNI;
#      programin ciktisi ayni. (Tek fark fonksiyonlar arasi dolgunun
#      baytlari: bolum sinirinda dolguyu birlestirici degil linker yaziyor.)
#   3. POZITIF KONTROL: TULPAR_AOT_BOLUM_SINAMA=ters nesneleri ters sirayla
#      linkler — yerlesim kayar ve 2. ayagin karsilastiricisi bunu
#      YAKALAMALI. Yakalamazsa 2. ayak hicbir sey olcmuyordu.
#   Ayrica: TULPAR_AOT_BOLUM=1 bolmeyi kapatiyor; ek nesneler (x.b1.o, ...)
#   linkten sonra siliniyor.
#
#   tests/bolumlu_emit.sh [tulpar]
set -u
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || [ -x "$TUL.exe" ] || { echo "HATA: '$TUL' yok" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
KOK="$(cd "$(dirname "$0")/.." && pwd)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }
# Karsilastirici: iki ikilinin fonksiyon adresleri + komut akisi + veri.
# Python'suz (Windows CI'da suites adiminda python3 bilerek yok): nm, objdump,
# objcopy, cmp, awk. Cikis 0 = ayni (tek satir ozet), 1 = farkli (ne farkli
# oldugunu basar; her satirda "farkli" gecer — pozitif kontrol buna bakar).
semboller() {  # yalniz kod sembolleri; bolumlu derlemenin disa acilan
               # adlari (__tulpar.b.<ozgun ad>) ozgun ada indirgenir
    nm -n --defined-only "$1" 2>/dev/null | awk '
        $2 ~ /^[tT]$/ && $3 !~ /^\./ {  # PE: her nesnenin .text BOLUM sembolu
                        n = $3; for (i = 4; i <= NF; i++) n = n " " $i
                        sub(/__tulpar\.b\./, "", n); print n, $1 }' | sort -u
}
akis() {  # adres sutunu, yorumlar ve sembol adlari atilmis komutlar; NOP
          # dolgusu (fonksiyonlar arasi; PE'de sifir dolgu dahil) yok sayilir
    objdump -d --no-show-raw-insn -j .text "$1" 2>/dev/null | awk '
        /^ *[0-9a-f]+:\t/ {
            sub(/^ *[0-9a-f]+:\t/, ""); sub(/ *#.*$/, ""); gsub(/ <[^>]*>/, "")
            gsub(/^[ \t]+|[ \t]+$/, "")
            if ($0 == "") next
            if ($0 ~ /^((data16|cs|ds|rex[.a-zA-Z]*)[ \t]+)*(nop[a-z]*|xchg +%ax,%ax|int3)([ \t]|$)/) next
            if ($0 ~ /^add +%al,\(%rax\)$/) next
            print }'
}
akis_sayisiz() {  # llvm-objdump; anlik degerler/adresler N, NOP dolgusu yok
    objdump -d --no-show-raw-insn "$1" 2>/dev/null | awk '
        /^ *[0-9a-f]+:[ \t]/ {
            sub(/^ *[0-9a-f]+:[ \t]+/, ""); sub(/[ \t]*(\/\/|;).*$/, ""); gsub(/<[^>]*>/, "")
            gsub(/#-?(0x[0-9a-f]+|[0-9]+)/, "#N"); gsub(/0x[0-9a-f]+/, "N"); gsub(/[ \t]+/, " ")
            gsub(/, #N\]/, "]")  # sayfa ici ofset 0 olunca llvm-objdump "[x8]" basar
            gsub(/^ | $/, "")
            if ($0 == "" || $0 ~ /^(nop|udf N)$/) next
            print }'
}
akis_adressiz() {  # GNU objdump; adresler / anlik degerler N, dolgu yok (lld ayagi)
    objdump -d --no-show-raw-insn -j .text "$1" 2>/dev/null | awk '
        /^ *[0-9a-f]+:\t/ {
            sub(/^ *[0-9a-f]+:\t/, ""); sub(/ *#.*$/, ""); gsub(/ <[^>]*>/, "")
            gsub(/0x[0-9a-f]+/, "N"); gsub(/(^|[ ,(])[0-9a-f]{4,}($|[ ,)])/, " N ")
            gsub(/[ \t]+/, " "); gsub(/^ | $/, "")
            if ($0 == "") next
            if ($0 ~ /^((data16|cs|ds|rex[.a-zA-Z]*) )*(nop[a-z]*|xchg %ax,%ax|int3)( |$)/) next
            print }'
}
bolum_bayt() { objcopy -O binary --only-section="$2" "$1" "$3" 2>/dev/null; }
karsilastir() {  # karsilastir <tek> <bol>
    local a=$1 b=$2 fark=0 n metin=""
    semboller "$a" > "$TMP/sa"; semboller "$b" > "$TMP/sb"
    n=$(wc -l < "$TMP/sa" | tr -d ' ')
    if [ "$n" -eq 0 ]; then echo "  nm sembol vermedi — farkli sayildi"; return 1; fi
    if ! cmp -s "$TMP/sa" "$TMP/sb"; then
        echo "  fonksiyon adresleri farkli ($n / $(wc -l < "$TMP/sb" | tr -d ' ') sembol; ornek: $(diff "$TMP/sa" "$TMP/sb" | grep '^[<>]' | head -2 | tr '\n' ' '))"
        fark=1
    fi
    if objdump --version 2>/dev/null | grep -q GNU; then
        bolum_bayt "$a" .text "$TMP/ta"; bolum_bayt "$b" .text "$TMP/tb"
        if cmp -s "$TMP/ta" "$TMP/tb"; then
            metin="; .text BAYT BAYT ayni"
        else
            local nb; nb=$(cmp -l "$TMP/ta" "$TMP/tb" 2>/dev/null | wc -l | tr -d ' ')
            akis "$a" > "$TMP/ka" & akis "$b" > "$TMP/kb"; wait
            if cmp -s "$TMP/ka" "$TMP/kb" && [ -s "$TMP/ka" ]; then
                metin="; .text komut akisi ayni ($nb farkli bayt yalniz NOP dolgusunda)"
            else
                echo "  .text komut akisi farkli ($(wc -l < "$TMP/ka" | tr -d ' ') / $(wc -l < "$TMP/kb" | tr -d ' ') komut; ilk fark: $(diff "$TMP/ka" "$TMP/kb" | grep '^[<>]' | head -2 | tr '\n' ' '))"
                fark=1
            fi
        fi
        local sec
        for sec in .rodata .rdata .data; do
            objdump -h "$a" 2>/dev/null | grep -q " $sec " || continue
            bolum_bayt "$a" $sec "$TMP/da"; bolum_bayt "$b" $sec "$TMP/db"
            cmp -s "$TMP/da" "$TMP/db" || { echo "  $sec baytlari farkli"; fark=1; }
        done
        metin="$metin; veri ayni"
    elif objdump --version 2>/dev/null | grep -qi llvm; then
        # macOS (llvm-objdump, Mach-O): adresi onemsiz sabitler her bolume
        # KOPYALANIYOR (llvm_bolum.cpp), yani dizgi adresleri — ve onlari
        # yukleyen adrp/add anlik degerleri — farkli. SAYILAR atilarak
        # karsilastiriliyor: GOT'a donen erisim (adrp+ldr / adrp+add) ya da
        # farkli komut sayisi yine gorunur; veri karsilastirmasi yok.
        akis_sayisiz "$a" > "$TMP/ka" & akis_sayisiz "$b" > "$TMP/kb"; wait
        if cmp -s "$TMP/ka" "$TMP/kb" && [ -s "$TMP/ka" ]; then
            metin="; komut akisi ayni (llvm-objdump, sayilar haric; veri karsilastirilmadi — Mach-O'da dizgiler kopyalaniyor)"
        else
            echo "  komut akisi farkli (sayilar haric; $(wc -l < "$TMP/ka" | tr -d ' ') / $(wc -l < "$TMP/kb" | tr -d ' ') komut; ilk fark: $(diff "$TMP/ka" "$TMP/kb" | grep '^[<>]' | head -2 | tr '\n' ' '))"
            fark=1
        fi
    else
        metin=" (objdump yok: komut akisi + veri karsilastirmasi ATLANDI; adresler karsilastirildi)"
    fi
    [ $fark -eq 0 ] || return 1
    echo "  $n fonksiyon ayni adreste$metin"
}

exe() { if [ -f "$1.exe" ]; then echo "$1.exe"; else echo "$1"; fi; }
# BAGLAYICI ld.bfd (2026-10-06): Linux'ta varsayilan link artik ld.lld
# (varsa; aot_link_driver). lld .eh_frame'i girdi nesnelerinden YENIDEN
# YAZMIYOR — bolum nesnelerinin FDE dolgulari kaldigi icin cikti birkac bayt
# buyuyor ve .text 64 B kayiyor (wings: .eh_frame +16 B, her fonksiyon +0x40,
# hizalama ayni). Bu kapinin iddiasi KOD URETIMI hakkinda ("bolmek makine
# kodunu degistirmez"); onu bayt bayt yeniden ureten bfd ile olcer. lld
# ayagi asagida ayri: komut akisi adresler haric ayni.
derle() {  # derle <kaynak> <cikti> [env...]
    local src=$1 out=$2; shift 2
    (cd "$TMP" && env TULPAR_AOT_NOCACHE=1 TULPAR_AOT_TIME=1 TULPAR_LD=bfd "$@" "$TUL" build "$src" "$out") > "$TMP/$out.log" 2>&1
}
bolum_satiri() { grep -m1 'AOT-BOLUM' "$TMP/$1.log"; }

# denetle <ad> <kaynak> <asgari_bolum> <azami_pay> <kos: evet|hayir> [BENCH_N]
denetle() {
    local ad=$1 src=$2 asg=$3 pay_azami=$4 kos=$5 bn=${6:-}
    if ! derle "$src" "${ad}_tek" TULPAR_AOT_BOLUM=1; then
        dustu "$ad: tek nesne derlemesi dustu"; tail -5 "$TMP/${ad}_tek.log" | sed 's/^/         /'; return
    fi
    if ! derle "$src" "${ad}_bol"; then
        dustu "$ad: bolumlu derleme dustu"; tail -5 "$TMP/${ad}_bol.log" | sed 's/^/         /'; return
    fi
    if bolum_satiri "${ad}_tek" >/dev/null; then dustu "$ad: TULPAR_AOT_BOLUM=1 iken bolundu"
    else gecti "$ad: TULPAR_AOT_BOLUM=1 tek nesne (eski yol)"; fi
    local satir n pay
    satir=$(bolum_satiri "${ad}_bol")
    n=$(sed -nE 's/.*\[AOT-BOLUM\] bolum=([0-9]+) .*/\1/p' <<<"$satir")
    pay=$(sed -nE 's/.*en_buyuk_pay=%([0-9]+).*/\1/p' <<<"$satir")
    if [ -n "$n" ] && [ "$n" -ge "$asg" ] && [ -n "$pay" ] && [ "$pay" -le "$pay_azami" ]; then
        gecti "$ad: $n bolum, en buyuk bolum komutlarin %$pay'i (esik: >= $asg bolum, <= %$pay_azami)"
    else
        dustu "$ad: bolunme beklenenden az: '${satir:-[AOT-BOLUM] satiri yok}' (esik >= $asg bolum, pay <= %$pay_azami)"
    fi
    # Bolumlu derlemede butun bolum nesneleri (x.o dahil — tek basina
    # linklenemeyen ilk bolum) linkten sonra silinir; tek nesne yolu x.o'yu
    # eskisi gibi birakir.
    if ls "$TMP/${ad}_bol".b*.o >/dev/null 2>&1 || [ -f "$TMP/${ad}_bol.o" ]; then
        dustu "$ad: bolum nesneleri linkten sonra kaldi"
    elif [ ! -f "$TMP/${ad}_tek.o" ]; then
        dustu "$ad: tek nesne yolu x.o'yu birakmadi (eski davranis degisti)"
    else
        gecti "$ad: bolum nesneleri linkten sonra silindi; tek nesne yolu x.o'yu birakiyor"
    fi
    local cikti
    if cikti=$(karsilastir "$(exe "$TMP/${ad}_tek")" "$(exe "$TMP/${ad}_bol")"); then
        gecti "$ad: ikili ayni — ${cikti#  }"
    else
        dustu "$ad: tek nesne ile bolumlu ikili FARKLI:"; echo "$cikti" | sed 's/^/       /'
    fi
    if [ "$kos" = evet ]; then
        local o1 o2
        o1=$(cd "$TMP" && BENCH_N=$bn "$(exe "$TMP/${ad}_tek")" 2>&1; echo "rc=$?")
        o2=$(cd "$TMP" && BENCH_N=$bn "$(exe "$TMP/${ad}_bol")" 2>&1; echo "rc=$?")
        if [ "$o1" = "$o2" ] && [ -n "$o1" ]; then gecti "$ad: iki ikilinin ciktisi ayni ($(tail -2 <<<"$o1" | tr '\n' ' '))"
        else dustu "$ad: ciktilar farkli"; diff <(echo "$o1") <(echo "$o2") | head -6 | sed 's/^/         /'; fi
    fi
}

denetle wings "$KOK/examples/wings_groups_test.tpr" 4 35 hayir
# lld AYAGI (Linux, ld.lld varsa — varsayilan link yolu): tek nesne ile
# bolumlu ikilinin komut akisi adresler ve anlik degerler HARIC ayni olmali.
if [ "$(uname -s)" = Linux ] && command -v ld.lld >/dev/null 2>&1 && objdump --version 2>/dev/null | grep -q GNU; then
    if derle "$KOK/examples/wings_groups_test.tpr" wings_tek_lld TULPAR_AOT_BOLUM=1 TULPAR_LD=lld &&
       derle "$KOK/examples/wings_groups_test.tpr" wings_bol_lld TULPAR_LD=lld; then
        akis_adressiz "$TMP/wings_tek_lld" > "$TMP/la" & akis_adressiz "$TMP/wings_bol_lld" > "$TMP/lb"; wait
        if [ -s "$TMP/la" ] && cmp -s "$TMP/la" "$TMP/lb"; then
            gecti "wings (lld): komut akisi adresler haric ayni ($(wc -l < "$TMP/la" | tr -d ' ') komut)"
            # Pozitif kontrol: ters link sirasi adressiz akista da gorunmeli.
            if derle "$KOK/examples/wings_groups_test.tpr" wings_ters_lld TULPAR_AOT_BOLUM_SINAMA=ters TULPAR_LD=lld; then
                akis_adressiz "$TMP/wings_ters_lld" > "$TMP/lt"
                if cmp -s "$TMP/la" "$TMP/lt"; then
                    dustu "wings (lld) pozitif kontrol: ters link sirasi adressiz akista gorulmedi — ayak olcmuyor"
                else
                    gecti "wings (lld) pozitif kontrol: ters link sirasi adressiz akista yakalandi"
                fi
            fi
        else
            dustu "wings (lld): komut akisi farkli ($(wc -l < "$TMP/la" | tr -d ' ') / $(wc -l < "$TMP/lb" | tr -d ' '); ilk fark: $(diff "$TMP/la" "$TMP/lb" | grep '^[<>]' | head -2 | tr '\n' ' '))"
        fi
    else
        dustu "wings (lld): derleme dustu"; tail -3 "$TMP/wings_bol_lld.log" | sed 's/^/         /'
    fi
fi
denetle nbody "$KOK/benchmarks/fair/nbody.tpr" 2 60 evet 1000
denetle sekil "$KOK/tests/array_shape_cache.test.tpr" 2 60 evet

# POZITIF KONTROL: ters link sirasi yerlesimi kaydirir — karsilastirici
# bunu gormezse 2. ayak olcmuyordu.
if derle "$KOK/examples/wings_groups_test.tpr" wings_ters TULPAR_AOT_BOLUM_SINAMA=ters; then
    # Yalniz cikis koduna bakmak yetmez: karsilastirici CALISAMADIYSA (arac
    # yok) da sifir olmayan doner ve "yakalandi" sanilir — CI'da tam boyle
    # oldu (Windows, python yok). Farkin ADI gorulmeli.
    karsilastir "$(exe "$TMP/wings_tek")" "$(exe "$TMP/wings_ters")" > "$TMP/ters.out"
    if [ $? -eq 0 ] || ! grep -qE "adresleri farkli|akisi farkli|baytlari farkli" "$TMP/ters.out"; then
        dustu "pozitif kontrol: nesneler TERS linklendigi halde fark gorulmedi — karsilastirici olcmuyor"
        sed 's/^/         /' "$TMP/ters.out"
    else
        gecti "pozitif kontrol: ters link sirasi yakalandi ($(head -1 "$TMP/ters.out" | sed 's/^ *//' | cut -c1-90))"
    fi
else
    dustu "pozitif kontrol derlemesi dustu"; tail -5 "$TMP/wings_ters.log" | sed 's/^/         /'
fi

if [ $fail -ne 0 ]; then
    echo -e "\033[0;31mBOLUMLU NESNE URETIMI KAPISI KIRMIZI\033[0m"; exit 1
fi
echo "bolumlu nesne uretimi kapisi: temiz"
