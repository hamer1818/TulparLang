#!/usr/bin/env python3
"""Motor köprüsü bindingleri TAZE mi: depodaki dört ÜRETİLMİŞ dosya, motorun
BUGÜNKÜ SPEC tablosundan üretilenle aynı mı?

NEDEN BU KAPI VAR
-----------------
`eng_*` ailesinin tek kaynağı motor deposundaki
`engine/tools/gen_engine_bindings.py` içindeki SPEC tablosu (bugün 175
builtin). Ürettiği dört dosya ise DERLEYİCİ deposunda yaşıyor:

    runtime/engine_bindings.cpp             aot_eng_*_ptr (VMValue ABI)
    src/aot/engine_builtins_table.inc       LLVM backend tablosu
    src/typeinfer/engine_builtins_sigs.inc  tip çıkarımı imzaları
    src/lsp/engine_builtins.inc             LSP tamamlama/hover

Motor ayrı depoya taşınana kadar sapma İMKÂNSIZDI — üreteci çalıştırmayan bir
değişiklik zaten aynı ağaçta derlenmiyordu. Submodule olunca bu, gerçek bir
hata sınıfı oluyor: motor tarafında bir imza değişir, buradaki üretilmiş
dosyalar tazelenmez, derleyici ESKİ imzayla derler ve HER ŞEY YEŞİL KALIR.

Bu depo bu sınıfı daha önce yaşadı — Tuzaklar 8aq: `aot_input`'un imzası
değişti, ön derlenmiş arşivler bayat kaldı. Sembolün VARLIĞINI sınayan denetim
"temiz" dedi, `wasm-ld` imza uyuşmazlığını UYARIYA indirip yine linkledi ve
hata ancak tarayıcıda, o çağrıya gelindiğinde patladı. Dersin özeti: "sembol
var mı?" sorusu bayatlığı GÖRMEZ; "üretilen ile depodaki AYNI mı?" sorusu
görür. Bu kapı ikincisini soruyor ve cevabı tek bir imzaya değil, dört
dosyanın tamamının içeriğine dayandırıyor.

NE ZAMAN ATLIYOR (ve neden GÖRÜNÜR atlıyor)
-------------------------------------------
Motor ağacı klonlanmamışsa kapı ATLANDI yazıp 0 döner: derleyici motor
olmadan da derlenebilmeli ve düzeltilemeyen bir kırmızı, kırmızıyı görmezden
gelmeyi öğretir (aynı gerekçe tests/dist_archive_audit.py'de android arşivleri
için de yazılı). Ama atlama SESSİZ DEĞİL — ekrana sebebiyle birlikte basılır.
Motor ağacı VAR ama üreteç bulunamıyorsa bu bir atlama değil KAPSAM KAYBIDIR
ve kırmızıdır.

POZİTİF KONTROL
---------------
`--kendini-sina`: üretilmiş dosyalardan birine sahte bir satır enjekte eder,
kapının KIRMIZI verdiğini doğrular, dosyayı bayt bayt geri alır ve kapının
yeniden YEŞİL olduğunu doğrular. Ölçmeyen kapı yoktur; bu bayrak kapının
ölçtüğünün kanıtıdır.
"""
import difflib
import os
import shutil
import subprocess
import sys
import tempfile

# Windows konsolu cp1254 ve Türkçe çıktı `UnicodeEncodeError` ile kapıyı
# ÖLÇMEDEN kırmızıya düşürebiliyor; aynı hata tests/silent_failure_probe.py
# başında da ölçüldü (2026-09-19, yerel Windows derlemesi). errors="replace":
# en kötü ihtimalle bir harf bozulur, kapı yine ÖLÇER.
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Motor başka bir yere klonlanmış olabilir (ayrı depo!) — senkron betiği bu
# değişkeni dışa aktarıyor ki iki taraf AYNI ağacı ölçsün.
ENGINE_DIR = os.environ.get("TULPAR_ENGINE_DIR") or os.path.join(ROOT, "engine")
GEN = os.path.join(ENGINE_DIR, "tools", "gen_engine_bindings.py")

# Üretecin yazdığı dosyalar. Liste elle yazılı ama KÖRLEMESİNE değil:
# kapsam_denetimi() üretecin gerçekten ürettiği dosya kümesini bu listeyle
# karşılaştırıyor, yani motor beşinci bir dosya üretmeye başlarsa kapı
# "temiz" demek yerine KAPSAM KAYBI diyor. (dist_archive_audit.py'deki
# "desen tutmazsa temiz deme, kapsamını kaybettiğini söyle" kuralı.)
OUTPUTS = [
    "runtime/engine_bindings.cpp",
    "src/aot/engine_builtins_table.inc",
    "src/typeinfer/engine_builtins_sigs.inc",
    "src/lsp/engine_builtins.inc",
]


def oku(path):
    """Dosyayı SATIR SONU farkını yok sayarak oku.

    Neden: üreteç metin kipinde yazıyor, yani Windows'ta CRLF üretir; depodaki
    kopyanın satır sonu ise checkout ayarına bağlıdır (`core.autocrlf`). İkisi
    ayrışırsa ham BAYT karşılaştırması HER SATIRI "farklı" gösterir — %100
    fark zaten tek başına harnessin bozuk olduğunun delilidir. Bu depo aynı
    sınıfı `.sahne` dosyalarında yaşadı (bkz. .gitattributes'taki not: kanonik
    biçim kapısı CRLF checkout yüzünden düşüyordu, bozulan yalnız checkout'tu).
    Ölçülmek istenen satır sonu değil, SPEC'in İÇERİĞİ.
    """
    with open(path, "rb") as f:
        return f.read().replace(b"\r\n", b"\n")


def uret(hedef_kok):
    """Üreteci hedef kökün İÇİNDE çalıştır.

    İki ayrıntı üreteç OKUNARAK öğrenildi:
      * Çıktı kökünü `sys.argv[1]` alıyor; verilmezse kendi konumundan üç
        dizin yukarısını kök sayıyor. Açıkça veriyoruz — motor başka bir yere
        klonlanmışsa o varsayılan YANLIŞ ağaca yazar.
      * `write()` yazdığı yolu `os.path.relpath(path)` ile basıyor; bu CWD'ye
        görelidir ve Windows'ta CWD ile hedef AYRI SÜRÜCÜDEYSE ValueError
        fırlatır (geçici dizin C:, depo D: olabilir). cwd=hedef_kok bunu
        kökünden imkânsız kılıyor.
    """
    return subprocess.run(
        [sys.executable, GEN, hedef_kok],
        cwd=hedef_kok, capture_output=True,
        encoding="utf-8", errors="replace")


def kapsam_denetimi(tmp):
    """Üreteç TAM OLARAK bildiğimiz dosyaları mı üretti? (fazla, eksik)"""
    uretilen = set()
    for dirpath, _dirs, files in os.walk(tmp):
        for fn in files:
            rel = os.path.relpath(os.path.join(dirpath, fn), tmp)
            uretilen.add(rel.replace(os.sep, "/"))
    return uretilen - set(OUTPUTS), set(OUTPUTS) - uretilen


def builtin_say(icerik):
    """Üretilmiş tablodan builtin sayısı — sabit yazmıyoruz, ÖLÇÜYORUZ."""
    return sum(1 for ln in icerik.split(b"\n") if ln.strip().startswith(b'{"eng_'))


def kontrol():
    rel_engine = os.path.relpath(ENGINE_DIR, ROOT).replace(os.sep, "/")

    # --- ÖNDENETİM: ağaç var mı -------------------------------------------
    # Klonlanmamış bir submodule BOŞ bir dizin bırakır; `isdir` tek başına
    # bunu "var" sayar, o yüzden içeriğe de bakıyoruz.
    if not os.path.isdir(ENGINE_DIR) or not os.listdir(ENGINE_DIR):
        print("motor binding tazeligi ATLANDI: engine submodule yok ya da bos "
              "(%s)" % rel_engine)
        print("  Klonla: git submodule update --init --recursive")
        print("  (derleyici motorsuz da derlenir; uretilmis dort dosya depoda "
              "duruyor — bu kosumda TAZELIKLERI OLCULMEDI)")
        return 0
    if not os.path.isfile(GEN):
        # Bu bir atlama DEĞİL: ağaç duruyor ama ölçemiyoruz.
        print("motor binding tazeligi BASARISIZ: motor agaci VAR ama uretec YOK")
        print("  Beklenen: %s" % GEN.replace(os.sep, "/"))
        print("  Uretecin yolu degismis olabilir. Kapi bu durumda 'temiz' demez;")
        print("  kapsamini kaybettigini soyler.")
        return 1

    tmp = tempfile.mkdtemp(prefix="tulpar_engbind_")
    try:
        r = uret(tmp)
        if r.returncode != 0:
            print("motor binding tazeligi BASARISIZ: uretec cikis %d verdi"
                  % r.returncode)
            for ln in (r.stderr or r.stdout or "").strip().split("\n")[-6:]:
                print("    %s" % ln)
            return 1

        fazla, eksik = kapsam_denetimi(tmp)
        if fazla or eksik:
            print("motor binding tazeligi BASARISIZ: uretecin cikti KUMESI "
                  "degismis")
            for p in sorted(fazla):
                print("    KAPI BILMIYOR (uretildi, denetlenmiyor): %s" % p)
            for p in sorted(eksik):
                print("    URETILMEDI (kapi bekliyordu): %s" % p)
            print("  OUTPUTS listesini tazele — yoksa yeni dosya SESSIZCE "
                  "bayatlar.")
            return 1

        farkli, yok = [], []
        for rel in OUTPUTS:
            depoda = os.path.join(ROOT, rel.replace("/", os.sep))
            if not os.path.isfile(depoda):
                yok.append(rel)
                continue
            beklenen = oku(os.path.join(tmp, rel.replace("/", os.sep)))
            mevcut = oku(depoda)
            if beklenen != mevcut:
                farkli.append((rel, beklenen, mevcut))

        if yok or farkli:
            print("motor binding tazeligi BASARISIZ: depodaki uretilmis "
                  "dosyalar motorun SPEC'inden GERIDE")
            for rel in yok:
                print("    EKSIK: %s (hic uretilmemis)" % rel)
            for rel, beklenen, mevcut in farkli:
                a = mevcut.decode("utf-8", "replace").split("\n")
                b = beklenen.decode("utf-8", "replace").split("\n")
                # `diff` IKILISINE BAGIMLI DEGILIZ: difflib stdlib'de. Bkz.
                # tests/fmt_audit.sh — orada `diff` eksikken denetim 148
                # dosyanin 148'ini suclamisti. Kapinin ayrinti basmasi harici
                # bir araca bagli olmamali.
                d = list(difflib.unified_diff(
                    a, b, fromfile="depoda/" + rel,
                    tofile="SPECten/" + rel, lineterm=""))
                print("    FARKLI: %s (%d diff satiri)" % (rel, max(len(d) - 2, 0)))
                for ln in d[:12]:
                    print("      %s" % ln)
                if len(d) > 12:
                    print("      ... (%d satir daha)" % (len(d) - 12))
            print("  Tazele:  bash tools/engine_bindings_sync.sh")
            print("  Sebep:   motor SPEC'i degisti ama uretilmis dosyalar")
            print("           tazelenmedi — derleyici ESKI imzayla derlerdi.")
            return 1

        n = builtin_say(oku(os.path.join(tmp, OUTPUTS[1].replace("/", os.sep))))
        print("motor binding tazeligi temiz (%d dosya, %d builtin — kaynak: %s)"
              % (len(OUTPUTS), n,
                 os.path.relpath(GEN, ROOT).replace(os.sep, "/")))
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def kendini_sina():
    """POZİTİF KONTROL: kapının gerçekten ÖLÇTÜĞÜNÜ kanıtla."""
    if (not os.path.isdir(ENGINE_DIR) or not os.listdir(ENGINE_DIR)
            or not os.path.isfile(GEN)):
        print("pozitif kontrol ATLANDI: motor agaci yok — enjeksiyon olculemez")
        return 0

    hedef = OUTPUTS[1]  # tablo .inc: kucuk, tek satirlik enjeksiyon yeter
    p = os.path.join(ROOT, hedef.replace("/", os.sep))
    if not os.path.isfile(p):
        print("pozitif kontrol BASARISIZ: %s yok" % hedef)
        return 1

    with open(p, "rb") as f:
        orijinal = f.read()

    print("pozitif kontrol: %s dosyasina sahte bir satir enjekte ediliyor..."
          % hedef)
    try:
        with open(p, "ab") as f:
            f.write(b'    {"eng_kapi_pozitif_kontrol", '
                    b'"aot_eng_kapi_pozitif_kontrol_ptr", 0},\n')
        if kontrol() == 0:
            print("POZITIF KONTROL BASARISIZ: kapi enjeksiyonu GORMEDI — "
                  "kapi hicbir sey olcmuyor")
            return 1
        print("pozitif kontrol: kapi KIRMIZI verdi (beklenen).")
    finally:
        # Geri alma BAYT BAYT — yeniden uretmiyoruz, cunku yeniden uretmek
        # enjeksiyonu duzeltirken depoda duran GERCEK bir sapmayi da
        # gizleyebilirdi. Amac dosyayi bulundugu hale dondurmek.
        with open(p, "wb") as f:
            f.write(orijinal)

    print("pozitif kontrol: dosya geri alindi, kapi yeniden kosuluyor...")
    if kontrol() != 0:
        print("POZITIF KONTROL BASARISIZ: geri almadan sonra kapi HALA kirmizi")
        return 1
    print("pozitif kontrol TAMAM: enjeksiyon=KIRMIZI, geri alma=YESIL.")
    return 0


def main():
    if "--kendini-sina" in sys.argv[1:] or "--selftest" in sys.argv[1:]:
        return kendini_sina()
    return kontrol()


if __name__ == "__main__":
    sys.exit(main())
