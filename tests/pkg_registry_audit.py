#!/usr/bin/env python3
"""`tulpar pkg install` REGISTRY yolu — yerel sahte registry'yle uçtan uca.

NEDEN
-----
tests/pkg_audit.sh yalnız `path:` zincirini sınıyor; registry yolu (aralık
çözümü, tulpar.lock, sha256, `.tpkg` çok dosyalı paket, önbellek) HİÇBİR
testte geçmiyordu. Sahte registry'yle ölçüldü (2026-09-27):
  * `.tpkg` paketleri önbelleğe HİÇ isabet etmiyordu — lock arşivin
    özetini tutuyor, önbellek denetimi açılmış `<ad>.tpr`'yi özetliyordu;
    her install yeniden indiriyordu.
  * aralık (`^1`) her install'da registry'ye karşı yeniden çözülüyordu:
    registry kapalıyken kilitli, diskte duran bağımlılık bile düşüyordu;
    yeni bir sürüm yayınlanınca `^1.0.0` SESSİZCE yükseliyor, lock yeniden
    yazılıyordu ("re-installs are reproducible" tutmuyordu).

NE ÖLÇÜLÜYOR (her adım İSTEK GÜNLÜĞÜYLE — "ağa gitmedi" iddiası sayılıyor)
  1. ilk install: `single@^1.0.0` (tek dosya) + `multi@^1` (.tpkg, 2 dosya)
     indiriliyor, tulpar.lock [resolved]+[checksums] yazılıyor, program iki
     paketi (ve multi'nin KARDEŞ import'unu) kullanıp doğru sonucu basıyor.
     POZİTİF KONTROL: bu adım istek YAPMAK zorunda (yoksa aşağıdaki "0 istek"
     iddiaları hiçbir şey kanıtlamaz).
  2. ikinci install: SIFIR istek (iki paket de önbellekten).
  3. registry 1.1.0 yayınlıyor: `install` kilitli 1.0.0'da kalıyor, 0 istek;
     `install --update` 1.1.0'a geçiyor ve lock güncelleniyor.
  4. registry KAPALI: install yine başarılı (lock + önbellek).
  5. vendor edilmiş .tpkg dosyası elle bozulunca önbellek ISKALIYOR ve dosya
     registry'den geri geliyor.
  6. lock'taki sha256 bozulunca install REDDEDİYOR (rc!=0, "mismatch").
  7. `pkg publish --dry-run` iki dosyalı projede `.tpkg` şekli seçiyor.

Kullanım: python3 tests/pkg_registry_audit.py [tulpar]
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SINGLE = {
    "1.0.0": "func tek(): int { return 10; }\n",
    "1.1.0": "func tek(): int { return 11; }\n",
}
MULTI_TPKG = json.dumps({
    "tpkg": 1, "name": "multi", "version": "1.2.0",
    "files": [
        {"path": "multi.tpr", "content": "import \"yardimci\";\nfunc m(): int { return 40 + s(); }\n"},
        {"path": "yardimci.tpr", "content": "func s(): int { return 9; }\n"},
    ],
})


class Registry:
    def __init__(self):
        self.log = []
        self.versions = {"single": ["1.0.0"], "multi": ["1.2.0"]}
        reg = self

        class H(BaseHTTPRequestHandler):
            def do_GET(self):
                reg.log.append(self.path)
                p = self.path.split("?")[0].strip("/").split("/")
                body = None
                if len(p) == 3 and p[:2] == ["v1", "packages"] and p[2] in reg.versions:
                    body = json.dumps({"name": p[2], "versions": reg.versions[p[2]]})
                elif len(p) == 6 and p[:2] == ["v1", "packages"] and p[3] == "versions" \
                        and p[5] == "source":
                    if p[2] == "single" and p[4] in SINGLE and p[4] in reg.versions["single"]:
                        body = SINGLE[p[4]]
                    elif p[2] == "multi" and p[4] == "1.2.0":
                        body = MULTI_TPKG
                if body is None:
                    self.send_response(404)
                    self.end_headers()
                    return
                b = body.encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(b)))
                self.end_headers()
                self.wfile.write(b)

            def log_message(self, *a):
                pass

        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.port = self.srv.server_address[1]
        self.th = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.th.start()

    def stop(self):
        self.srv.shutdown()
        self.srv.server_close()


def run(exe, args, cwd):
    r = subprocess.run([exe] + args, cwd=cwd, capture_output=True, text=True,
                       env=dict(os.environ, LC_ALL="C", DISPLAY=""))
    return r.returncode, r.stdout + r.stderr


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "tulpar")
    if not os.path.isfile(exe) and os.path.isfile(exe + ".exe"):
        exe += ".exe"
    exe = os.path.abspath(exe)
    work = tempfile.mkdtemp(prefix="tulpar_pkgreg_")
    reg = Registry()
    fails = []
    ok_n = 0

    def check(cond, what, detail=""):
        nonlocal ok_n
        if cond:
            ok_n += 1
            print("  gecti  " + what)
        else:
            fails.append(what)
            print("  DUSTU  " + what + (("\n" + "\n".join("      " + l for l in detail.splitlines()[-8:])) if detail else ""))

    try:
        proj = os.path.join(work, "proje")
        os.mkdir(proj)
        with open(os.path.join(proj, "tulpar.toml"), "w") as fh:
            fh.write('name = "proje"\nversion = "0.1.0"\n\n[registry]\n'
                     'url = "http://127.0.0.1:%d"\n\n[dependencies]\n'
                     'single = "^1.0.0"\nmulti = "^1"\n' % reg.port)
        with open(os.path.join(proj, "main.tpr"), "w") as fh:
            fh.write('import "single";\nimport "multi";\nprint(tek() + m());\n')
        lock = os.path.join(proj, "tulpar.lock")

        # 1) ilk install
        rc, out = run(exe, ["pkg", "install"], proj)
        n1 = len(reg.log)
        check(rc == 0 and n1 >= 4, "ilk install registry'den indirdi (%d istek — pozitif kontrol)" % n1, out)
        lock_text = open(lock).read() if os.path.exists(lock) else ""
        check("[resolved]" in lock_text and "[checksums]" in lock_text
              and "/versions/1.0.0/source" in lock_text and "/versions/1.2.0/source" in lock_text,
              "tulpar.lock [resolved]+[checksums] yazildi (1.0.0, 1.2.0)", lock_text)
        check(os.path.exists(os.path.join(proj, "tulpar_modules/multi/yardimci.tpr")),
              ".tpkg iki dosyayla acildi")
        rc, out = run(exe, ["main.tpr"], proj)
        check(rc == 0 and out.strip().splitlines()[-1:] == ["59"],
              "program iki paketi + kardes import'u kullaniyor (10 + 40 + 9 = 59)", out)

        # 2) ikinci install: sifir istek
        before = len(reg.log)
        rc, out = run(exe, ["pkg", "install"], proj)
        new = reg.log[before:]
        check(rc == 0 and not new, "ikinci install: 0 istek (tek dosya + .tpkg onbellekten)",
              out + "\nistekler: %s" % new)

        # 3) yeni surum yayinlandi: kilit tutar, --update yukseltir
        reg.versions["single"].append("1.1.0")
        before = len(reg.log)
        rc, out = run(exe, ["pkg", "install"], proj)
        new = reg.log[before:]
        check(rc == 0 and not new and "/versions/1.0.0/source" in open(lock).read(),
              "1.1.0 yayinlaninca install kilitli 1.0.0'da kaldi, 0 istek",
              out + "\nistekler: %s" % new)
        rc, out = run(exe, ["pkg", "install", "--update"], proj)
        lt = open(lock).read()
        check(rc == 0 and "/versions/1.1.0/source" in lt,
              "install --update 1.1.0'a gecti, lock guncellendi", out + "\n" + lt)
        rc, out = run(exe, ["main.tpr"], proj)
        check(rc == 0 and out.strip().splitlines()[-1:] == ["60"],
              "yukseltilen surum kullaniliyor (11 + 49 = 60)", out)

        # 4) registry kapali
        reg.stop()
        rc, out = run(exe, ["pkg", "install"], proj)
        check(rc == 0, "registry KAPALI: kilitli + diskteki bagimliliklar kuruldu", out)
        reg = Registry()
        # manifest'i yeni porta cevir (lock URL'leri eski portu tasiyor —
        # yeni porttan tam cozum bekleniyor)
        with open(os.path.join(proj, "tulpar.toml")) as fh:
            man = fh.read()
        man = re.sub(r"127\.0\.0\.1:\d+", "127.0.0.1:%d" % reg.port, man)
        with open(os.path.join(proj, "tulpar.toml"), "w") as fh:
            fh.write(man)
        reg.versions["single"].append("1.1.0")
        rc, out = run(exe, ["pkg", "install"], proj)
        check(rc == 0, "registry adresi degisince yeni adresten cozuldu", out)

        # 5) vendor edilmis .tpkg dosyasi bozulunca onbellek iskaliyor
        yard = os.path.join(proj, "tulpar_modules/multi/yardimci.tpr")
        with open(yard, "w") as fh:
            fh.write("func s(): int { return 0; }\n")
        before = len(reg.log)
        rc, out = run(exe, ["pkg", "install"], proj)
        new = reg.log[before:]
        restored = open(yard).read() == "func s(): int { return 9; }\n"
        check(rc == 0 and any("multi/versions/1.2.0/source" in x for x in new) and restored,
              "bozulan .tpkg dosyasi: onbellek iskaladi, registry'den geri geldi",
              out + "\nistekler: %s" % new)

        # 6) lock sha256 bozulunca reddeder
        lt = open(lock).read()
        bad = re.sub(r'(single = ")[0-9a-f]{64}', r"\g<1>" + "0" * 64, lt)
        with open(lock, "w") as fh:
            fh.write(bad)
        rc, out = run(exe, ["pkg", "install"], proj)
        check(rc != 0 and "mismatch" in out, "lock sha256 bozulunca install REDDEDIYOR", out)

        # 8) AYNA (K251): birincil registry OLU, `mirrors` canli. Pozitif
        #    kontrol once: aynasiz ayni manifest DUSMELI (yoksa aynanin
        #    "isledigi" bir sey kanitlamaz).
        import socket as _s
        olu = _s.socket()
        olu.bind(("127.0.0.1", 0))
        olu_port = olu.getsockname()[1]
        olu.close()  # port bos: baglanti reddedilir
        ay = os.path.join(work, "ayna")
        os.mkdir(ay)
        def ayna_manifest(mirrors):
            with open(os.path.join(ay, "tulpar.toml"), "w") as fh:
                fh.write('name = "ayna"\nversion = "0.1.0"\n\n[registry]\n'
                         'url = "http://127.0.0.1:%d"\n%s\n[dependencies]\n'
                         'single = "^1.0.0"\n' % (olu_port, mirrors))
        ayna_manifest("")
        rc, out = run(exe, ["pkg", "install"], ay)
        check(rc != 0, "kontrol: olu registry, ayna YOK -> install dusuyor", out)
        ayna_manifest('mirrors = ["http://127.0.0.1:%d"]\n' % reg.port)
        before = len(reg.log)
        rc, out = run(exe, ["pkg", "install"], ay)
        lt = open(os.path.join(ay, "tulpar.lock")).read() if os.path.exists(os.path.join(ay, "tulpar.lock")) else ""
        check(rc == 0 and len(reg.log) > before and "127.0.0.1:%d/v1/packages/single" % reg.port in lt,
              "olu registry + canli ayna -> ayna kullanildi, lock aynayi kaydetti", out + "\n" + lt)
        rc, out = run(exe, ["pkg", "add", "multi@^1"], ay)
        man = open(os.path.join(ay, "tulpar.toml")).read()
        check(rc == 0 and 'mirrors = ["http://127.0.0.1:%d"]' % reg.port in man,
              "pkg add manifesti yeniden yazinca mirrors korunuyor", man)
        bozuk = man.replace('mirrors = ["', 'mirrors = "', 1)
        with open(os.path.join(ay, "tulpar.toml"), "w") as fh:
            fh.write(bozuk)
        rc, out = run(exe, ["pkg", "install"], ay)
        check(rc != 0 and "mirrors must be an array" in out,
              "bozuk mirrors (dizi degil) sessizce yutulmuyor", out)

        # 9) DEPODAKI ORNEK (K171): examples/pkg_demo/ — tulpar.toml + lock +
        #    vendor edilmis tulpar_modules/demo. Gercek registry URL'sini
        #    tasiyor ama install AGA CIKMAMALI: diskteki dosyanin ozeti
        #    lock'la tutuyor. "cached" satiri bunun kaniti (indirme olsa
        #    satir "installed ... from"). Ornek bozulursa (lock ya da vendor
        #    dosyasi degisirse) bu denetim onu yakalar.
        ornek = os.path.join(work, "pkg_demo")
        shutil.copytree(os.path.join(ROOT, "examples", "pkg_demo"), ornek)
        rc, out = run(exe, ["pkg", "install"], ornek)
        check(rc == 0 and "(cached, sha256 matches lockfile)" in out,
              "examples/pkg_demo: install agsiz — vendor dosyasi lock'la tutuyor", out)
        rc, out = run(exe, ["main.tpr"], ornek)
        check(rc == 0 and out.strip().splitlines()[-2:] ==
              ["hello from demo@1.0.1", "goodbye from demo@1.0.1"],
              "examples/pkg_demo: program vendor edilmis paketi kullaniyor", out)

        # 7) publish --dry-run: iki dosyali proje -> .tpkg
        pub = os.path.join(work, "yayin")
        os.mkdir(pub)
        with open(os.path.join(pub, "tulpar.toml"), "w") as fh:
            fh.write('name = "yayin"\nversion = "0.1.0"\n\n[registry]\n'
                     'url = "http://127.0.0.1:%d"\n' % reg.port)
        with open(os.path.join(pub, "yayin.tpr"), "w") as fh:
            fh.write('import "ek";\nfunc y(): int { return e(); }\n')
        with open(os.path.join(pub, "ek.tpr"), "w") as fh:
            fh.write("func e(): int { return 1; }\n")
        rc, out = run(exe, ["pkg", "publish", "--dry-run"], pub)
        check(rc == 0 and ".tpkg" in out, "publish --dry-run iki dosyali projede .tpkg secti", out)
    finally:
        try:
            reg.stop()
        except Exception:
            pass
        shutil.rmtree(work, ignore_errors=True)

    if fails:
        print("pkg registry denetimi DUSTU (%d/%d)" % (len(fails), len(fails) + ok_n))
        return 1
    print("pkg registry denetimi temiz (%d denetim: aralik+lock+sha256, .tpkg, onbellek, "
          "--update, cevrimdisi, bozulma, ayna, ornek proje, publish sekli)" % ok_n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
