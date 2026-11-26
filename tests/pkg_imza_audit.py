#!/usr/bin/env python3
"""PAKET IMZASI KAPISI (2026-11-26) — `tulpar pkg keygen/sign/verify` ve
`[trust]` ile imzali kurulum, yerel HTTP sunucusuyla uctan uca.

NEDEN: tulpar.lock'taki sha256 ilk indirmenin YAZARIN paketi oldugunu
soylemez; ele gecirilmis bir sunucu ilk kurulumda sahte paket verebilir.

OLCULEN (her adim sunucunun ISTEK GUNLUGUYLE):
  1. keygen/sign/verify: gecerli imza dogrulanir; bir bayti degismis dosya
     ve baska anahtar REDDEDILIR.
  2. [trust] yok: davranis eski — `.sig` HIC istenmez (gunluk), kurulur.
  3. [trust] + gecerli imza: kurulur, kilit [signatures]'ta imzaciyi tutar.
  4. sunucu paketi DEGISTIRIR (ayni .sig): kurulum REDDEDILIR, kilit eskisi.
  5. guvenilmeyen anahtarla imzali: REDDEDILIR.
  6. .sig yok + require_signatures = true: REDDEDILIR; false: uyari + kurulur.
  7. onbellek: sunucu kapaliyken imzaci guvenilir -> onbellekten kurulur;
     imzaci [trust]'tan cikarilinca (require) onbellek GECERSIZ -> yeniden
     indirmeye calisir ve kapali sunucuda duser (onbellek imzaciya bakiyor).
"""
import os
import shutil
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TUL = os.path.join(ROOT, sys.argv[1] if len(sys.argv) > 1 else "tulpar")
fails = []


def check(ad, kosul, detay=""):
    print("  %s  %s %s" % ("gecti" if kosul else "DUSTU", ad, detay))
    if not kosul:
        fails.append(ad)


class Sunucu:
    def __init__(self):
        self.files = {}
        self.log = []
        sun = self

        class H(BaseHTTPRequestHandler):
            def do_GET(self):
                sun.log.append(self.path)
                body = sun.files.get(self.path)
                if body is None:
                    self.send_response(404); self.end_headers(); return
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *a):
                pass

        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.port = self.srv.server_address[1]
        self.t = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.t.start()

    def url(self, p):
        return "http://127.0.0.1:%d%s" % (self.port, p)

    def stop(self):
        self.srv.shutdown()
        self.srv.server_close()


def run(args, cwd):
    return subprocess.run([TUL] + args, cwd=cwd, capture_output=True, text=True, timeout=120)


def main():
    probe = run(["pkg", "keygen", os.path.join(tempfile.gettempdir(), "imza_probe_%d.key" % os.getpid())], "/")
    if probe.returncode != 0 and "OpenSSL" in probe.stderr:
        print("ATLANDI: pkg_imza_audit — bu tulpar OpenSSL'siz derlenmis")
        return 0
    os.unlink(os.path.join(tempfile.gettempdir(), "imza_probe_%d.key" % os.getpid()))
    d = tempfile.mkdtemp(prefix="imza_")
    try:
        # 1) anahtarlar ve imza
        a = run(["pkg", "keygen", "yazar.key"], d)
        b = run(["pkg", "keygen", "yabanci.key"], d)
        pub_a, pub_b = a.stdout.strip(), b.stdout.strip()
        check("keygen genel anahtar basar", pub_a.startswith("ed25519:") and pub_b.startswith("ed25519:") and pub_a != pub_b)
        check("ozel anahtar 0600", (os.stat(os.path.join(d, "yazar.key")).st_mode & 0o777) == 0o600)
        paket = b"func selam(): str { return \"imzali\"; }\n"
        open(os.path.join(d, "selam.tpr"), "wb").write(paket)
        s = run(["pkg", "sign", "selam.tpr", "--key", "yazar.key"], d)
        check("sign .sig yazar", s.returncode == 0 and os.path.exists(os.path.join(d, "selam.tpr.sig")))
        v = run(["pkg", "verify", "selam.tpr", "selam.tpr.sig", pub_a], d)
        check("verify gecerli imza", v.returncode == 0)
        open(os.path.join(d, "bozuk.tpr"), "wb").write(paket.replace(b"imzali", b"imzalI"))
        v2 = run(["pkg", "verify", "bozuk.tpr", "selam.tpr.sig", pub_a], d)
        v3 = run(["pkg", "verify", "selam.tpr", "selam.tpr.sig", pub_b], d)
        check("verify: bir bayt degisince RED", v2.returncode != 0)
        check("verify: baska anahtar RED", v3.returncode != 0)
        sig_a = open(os.path.join(d, "selam.tpr.sig"), "rb").read()
        run(["pkg", "sign", "selam.tpr", "--key", "yabanci.key", "-o", "selam.yabanci.sig"], d)
        sig_b = open(os.path.join(d, "selam.yabanci.sig"), "rb").read()

        srv = Sunucu()
        proj = os.path.join(d, "proje")
        os.makedirs(proj)
        open(os.path.join(proj, "ana.tpr"), "w").write('import "selam";\nprint(selam());\n')

        def toml(trust=None, require=False):
            t = 'name = "proje"\nversion = "0.1.0"\n\n'
            t += '[dependencies]\nselam = "url:%s"\n' % srv.url("/selam.tpr")
            if trust is not None:
                t += '\n[trust]\nkeys = [%s]\nrequire_signatures = %s\n' % (
                    ", ".join('"%s"' % k for k in trust), "true" if require else "false")
            open(os.path.join(proj, "tulpar.toml"), "w").write(t)

        def temiz():
            shutil.rmtree(os.path.join(proj, "tulpar_modules"), ignore_errors=True)
            p = os.path.join(proj, "tulpar.lock")
            if os.path.exists(p): os.unlink(p)

        # 2) trust yok
        srv.files = {"/selam.tpr": paket, "/selam.tpr.sig": sig_a}
        toml(None); temiz(); srv.log.clear()
        r = run(["pkg", "install"], proj)
        check("trust yok: kurulur, .sig istenmez", r.returncode == 0 and "/selam.tpr.sig" not in srv.log, str(srv.log))

        # 3) trust + gecerli
        toml([pub_a], True); temiz(); srv.log.clear()
        r = run(["pkg", "install"], proj)
        lock = open(os.path.join(proj, "tulpar.lock")).read() if os.path.exists(os.path.join(proj, "tulpar.lock")) else ""
        check("gecerli imza: kurulur", r.returncode == 0, r.stderr[-300:])
        check("kilit [signatures] imzaciyi tutar", "[signatures]" in lock and pub_a in lock)
        out = run(["ana.tpr"], proj)
        check("kurulan paket calisir", out.stdout.strip() == "imzali", out.stdout + out.stderr[-200:])

        # 4) sunucu paketi degistirir (ayni imza)
        srv.files["/selam.tpr"] = paket.replace(b"imzali", b"KOTU")
        temiz()
        r = run(["pkg", "install"], proj)
        check("degistirilmis paket REDDEDILIR", r.returncode != 0 and "dogrulanmadi" in r.stderr, r.stderr[-200:])
        check("reddedilen paket diske yazilmadi",
              not os.path.exists(os.path.join(proj, "tulpar_modules", "selam", "selam.tpr")))

        # 5) guvenilmeyen anahtar
        srv.files = {"/selam.tpr": paket, "/selam.tpr.sig": sig_b}
        temiz()
        r = run(["pkg", "install"], proj)
        check("guvenilmeyen anahtarla imzali REDDEDILIR", r.returncode != 0)

        # 6) imza yok
        srv.files = {"/selam.tpr": paket}
        temiz()
        r = run(["pkg", "install"], proj)
        check("imza yok + require: REDDEDILIR", r.returncode != 0 and "imza yok" in r.stderr)
        toml([pub_a], False); temiz()
        r = run(["pkg", "install"], proj)
        check("imza yok + require=false: uyari + kurulur", r.returncode == 0 and "imzasiz" in r.stderr)

        # 7) onbellek imzaciya bakiyor
        srv.files = {"/selam.tpr": paket, "/selam.tpr.sig": sig_a}
        toml([pub_a], True); temiz()
        run(["pkg", "install"], proj)
        srv.stop()
        r = run(["pkg", "install"], proj)
        check("sunucu kapali, imzaci guvenilir: onbellekten", r.returncode == 0 and "cached" in r.stdout, r.stderr[-200:])
        toml([pub_b], True)
        r = run(["pkg", "install"], proj)
        check("imzaci trust'tan cikinca onbellek gecersiz (kapali sunucuda duser)", r.returncode != 0)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    if fails:
        print("FAIL: pkg_imza_audit — %d dustu" % len(fails))
        return 1
    print("PASS: pkg_imza_audit — keygen/sign/verify, imzali kurulum, degistirilmis/yabanci/imzasiz "
          "paket reddi, kilit imzacisi, onbellek imzaciya bakiyor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
