#!/usr/bin/env bash
# ETIKETTE YENIDEN KULLANIM KARARI — yayinlanacak bayt = PR'da test edilen bayt mi?
#
# NIYE (2026-10-30): etiket isi uc platformu sifirdan derleyip testleri yeniden
# kosuyordu (~10-11 dk). PR CI ayni agaci birlesince cikacak surumle
# (tools/ci_surum.sh) zaten derleyip test etti; ayni baytlari yayinlamak hem
# hizli hem DAHA dogru (yayinlanan = olculen). Ama yalniz su HEPSI tutarsa:
#   1. etiket commit'inin basliginda (#N) var ve PR #N main'e birlesmis, birlesme
#      commit'i etiketin commit'i;
#   2. etiketin agaci == PR koşumunun DERLEDIGI agac (koşumun kaydettigi
#      derleme-bilgisi; strict korumadan bagimsiz kanit);
#   3. o koşum yesil;
#   4. koşumun gomdugu surum == etiket.
# Biri tutmazsa "sifirdan" — eski yol. Karar asla sessiz degil: sebep basilir.
# Gomulu surum indirilen ikilide AYRICA calistirilarak olculur (is akisinda,
# tools/surum_denetle.sh); bu betik yalniz kaydi karsilastirir.
#
#   tools/yeniden_kullanim_karar.sh <etiket> <etiket_sha> <etiket_agac> \
#       <pr_no> <pr_durum> <pr_birlesme_sha> <kosum_id> <kosum_sonuc> \
#       <pr_agac> <pr_surum>
#   -> stdout: "karar=yeniden" ya da "karar=sifirdan", ardindan "sebep=..."
#   tools/yeniden_kullanim_karar.sh --oz-test
set -uo pipefail

karar() {
  local etiket="$1" etiket_sha="$2" etiket_agac="$3" pr_no="$4" pr_durum="$5"
  local pr_birlesme="$6" kosum="$7" kosum_sonuc="$8" pr_agac="$9" pr_surum="${10}"
  sifirdan() { echo "karar=sifirdan"; echo "sebep=$1"; }
  [ -n "$etiket" ] || { sifirdan "etiket yok"; return; }
  [ -n "$pr_no" ] || { sifirdan "etiket commit'inin basliginda (#N) yok"; return; }
  [ "$pr_durum" = "MERGED" ] || { sifirdan "PR #$pr_no birlesmemis (durum '${pr_durum:-?}')"; return; }
  [ -n "$etiket_sha" ] && [ "$pr_birlesme" = "$etiket_sha" ] \
    || { sifirdan "PR #$pr_no birlesme commit'i (${pr_birlesme:0:9}) etiketin commit'i (${etiket_sha:0:9}) degil"; return; }
  [ -n "$kosum" ] || { sifirdan "PR #$pr_no ucu icin yesil Build (LLVM) koşumu yok"; return; }
  [ "$kosum_sonuc" = "success" ] || { sifirdan "PR #$pr_no koşumu $kosum yesil degil ('${kosum_sonuc:-?}')"; return; }
  [ -n "$pr_agac" ] || { sifirdan "koşum $kosum derleme-bilgisi (agac) kaydetmemis"; return; }
  [ -n "$etiket_agac" ] && [ "$pr_agac" = "$etiket_agac" ] \
    || { sifirdan "agac farkli: etiket ${etiket_agac:0:12}, koşumun derledigi ${pr_agac:0:12}"; return; }
  [ "$pr_surum" = "$etiket" ] \
    || { sifirdan "gomulu surum '${pr_surum:-bos}' != etiket '$etiket' (tahmin tutmadi)"; return; }
  echo "karar=yeniden"
  echo "sebep=PR #$pr_no koşumu $kosum: agac ${pr_agac:0:12} = etiket agaci, gomulu surum $pr_surum = etiket"
}

oz_test() {
  local h=0
  durum() {   # $1 ad, $2 beklenen karar, geri kalan: karar() argumanlari
    local ad="$1" bek="$2"; shift 2
    local c; c="$(karar "$@" | head -1)"
    if [ "$c" = "karar=$bek" ]; then echo "  gecti  $ad -> $bek"
    else echo "  DUSTU  $ad: $c (beklenen $bek) — $(karar "$@" | tail -1)"; h=1; fi
  }
  local E=v3.43.0 S=aaaa1111 A=agac0001
  durum "hepsi tutuyor" yeniden           $E $S $A 491 MERGED $S 777 success $A $E
  durum "KONTROL: agac farkli" sifirdan    $E $S $A 491 MERGED $S 777 success agac0002 $E
  durum "KONTROL: surum farkli (tahmin)" sifirdan $E $S $A 491 MERGED $S 777 success $A v3.42.1
  durum "KONTROL: surum bos (tahminsiz)" sifirdan $E $S $A 491 MERGED $S 777 success $A ""
  durum "KONTROL: koşum kirmizi" sifirdan  $E $S $A 491 MERGED $S 777 failure $A $E
  durum "KONTROL: koşum yok" sifirdan      $E $S $A 491 MERGED $S "" "" $A $E
  durum "KONTROL: PR birlesmemis" sifirdan $E $S $A 491 OPEN "" 777 success $A $E
  durum "KONTROL: birlesme baska commit" sifirdan $E $S $A 491 MERGED bbbb2222 777 success $A $E
  durum "KONTROL: baslikta (#N) yok" sifirdan $E $S $A "" "" "" "" "" "" ""
  durum "KONTROL: koşum agac kaydetmemis" sifirdan $E $S $A 491 MERGED $S 777 success "" $E
  if [ $h -ne 0 ]; then echo "yeniden_kullanim_karar oz-test DUSTU"; return 1; fi
  echo "yeniden_kullanim_karar oz-test temiz"
}

if [ "${1:-}" = "--oz-test" ]; then oz_test; exit $?; fi
[ $# -eq 10 ] || { echo "kullanim: $0 <etiket> <etiket_sha> <etiket_agac> <pr_no> <pr_durum> <pr_birlesme_sha> <kosum_id> <kosum_sonuc> <pr_agac> <pr_surum> | --oz-test" >&2; exit 2; }
karar "$@"
