"""Uctan uca kapi: `import "smtp"` gercek bir SMTP sunucusuna gonderiyor mu,
ve yanlis/sahte sertifikada, indirgemede, enjeksiyonda GERCEKTEN reddediyor mu?

Bagimliliksiz (aiosmtpd yok): asagidaki `Sunucu` STARTTLS / dogrudan TLS /
AUTH PLAIN+LOGIN / MAIL / RCPT / DATA konusan kucuk bir RFC 5321 sunucusu.
Sertifikalar `openssl` ile anlik uretiliyor: bir test CA'si, ona imzali
`localhost` ve `baska.example` yapraklari, bir de CA disi kendinden imzali
`localhost`. Istemci CA'yi TULPAR_CA_BUNDLE ile taniyor (http_request ile ayni
dogrulama yolu).

Kontrol edilenler SUNUCU TARAFINDAN, istemcinin "ok" demesine guvenmeden:
iletinin gercekten geldigi, AUTH ve DATA'nin TLS altinda oldugu, Turkce
konunun RFC 2047'den geri cozuldugu, nokta doldurmanin geri alindigi,
Reply-To'daki CR/LF'nin yeni baslik dogurmadigi. Reddedilmesi gereken her
senaryoda sunucu ne AUTH ne MAIL gormemis olmali.

openssl ya da TLS'li ikili yoksa `SKIP:` ile cikar (exit 0).
"""

from __future__ import annotations

import base64
import email
import email.header
import email.policy
import os
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time


def find_tulpar_exe() -> str:
    env = os.environ.get("TULPAR_EXE")
    if env and os.path.exists(env):
        return os.path.abspath(env)
    for c in ["./tulpar", "tulpar"]:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise SystemExit("tulpar ikilisi yok (once ./build.sh, ya da TULPAR_EXE)")


USER = "gonderen@example.com"
PASS = "gizli-parola-123"


class Oturum:
    def __init__(self):
        self.tls_at_auth = None
        self.tls_at_data = None
        self.auth = []          # (mekanizma, kullanici, parola)
        self.mail_from = None
        self.rcpts = []
        self.data = None
        self.commands = []


class Sunucu:
    """Tek baglantilik oturumlari sirayla isleyen kucuk SMTP sunucusu.

    kip: "starttls" | "tls" (dogrudan) | "plain-only" (STARTTLS sunmaz) |
         "inject" (STARTTLS yanitinin arkasina duz metin bayt ekler)
    auth: "plain" | "login" (yalniz LOGIN ilan edilir)
    """

    def __init__(self, ctx: ssl.SSLContext, kip: str, auth: str = "plain",
                 reject_rcpt: str = ""):
        self.ctx = ctx
        self.kip = kip
        self.auth_kip = auth
        self.reject_rcpt = reject_rcpt
        self.sessions: list[Oturum] = []
        self.ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.ls.bind(("127.0.0.1", 0))
        self.ls.listen(8)
        self.port = self.ls.getsockname()[1]
        self.t = threading.Thread(target=self._loop, daemon=True)
        self.t.start()

    def _loop(self):
        while True:
            try:
                c, _ = self.ls.accept()
            except OSError:
                return
            c.settimeout(10)
            s = Oturum()
            self.sessions.append(s)
            try:
                self._serve(c, s)
            except Exception as e:  # istemci el sikismasini kesince vb.
                s.commands.append("!" + type(e).__name__)
            finally:
                try:
                    c.close()
                except OSError:
                    pass

    def _serve(self, c, s: Oturum):
        tls = False
        if self.kip == "tls":
            c = self.ctx.wrap_socket(c, server_side=True)
            tls = True
        f = c.makefile("rwb", buffering=0)

        def send(line: str):
            f.write((line + "\r\n").encode())

        def readline() -> str:
            ln = f.readline(70000)
            if not ln:
                raise EOFError
            return ln.decode("utf-8", "replace").rstrip("\r\n")

        send("220 test.local ESMTP hazir")
        while True:
            line = readline()
            s.commands.append(line if not line.upper().startswith("AUTH") else line.split(" ")[0] + " " + line.split(" ")[1])
            up = line.upper()
            if up.startswith("EHLO"):
                caps = ["250-test.local merhaba"]
                if not tls and self.kip in ("starttls", "inject"):
                    caps.append("250-STARTTLS")
                caps.append("250-SIZE 1000000")
                if self.auth_kip == "login":
                    caps.append("250-AUTH LOGIN")
                else:
                    caps.append("250-AUTH PLAIN LOGIN")
                caps.append("250 8BITMIME")
                f.write(("\r\n".join(caps) + "\r\n").encode())
            elif up == "STARTTLS":
                if self.kip == "inject":
                    # Yanit enjeksiyonu: TLS'ten ONCE okunan bayt TLS
                    # sonrasina ait sanilirsa saldirgan komut yanitlarini
                    # taklit eder. Istemci bunu reddetmeli.
                    # Gercek saldiridaki gibi sunucu ardindan TLS'e geciyor:
                    # denetim yoksa istemci sahte "250 AUTH PLAIN"i TLS
                    # sonrasi EHLO yaniti sanip parolayi gonderir.
                    f.write(b"220 basla\r\n250-sahte\r\n250 AUTH PLAIN\r\n")
                    c = self.ctx.wrap_socket(c, server_side=True)
                    tls = True
                    f = c.makefile("rwb", buffering=0)
                    continue
                send("220 TLS'e gec")
                c = self.ctx.wrap_socket(c, server_side=True)
                tls = True
                f = c.makefile("rwb", buffering=0)
            elif up.startswith("AUTH PLAIN"):
                s.tls_at_auth = tls
                raw = base64.b64decode(line.split(" ", 2)[2])
                _, u, p = raw.split(b"\x00")
                s.auth.append(("PLAIN", u.decode(), p.decode()))
                if u.decode() == USER and p.decode() == PASS:
                    send("235 2.7.0 tamam")
                else:
                    send("535 5.7.8 kimlik reddedildi")
            elif up == "AUTH LOGIN":
                s.tls_at_auth = tls
                send("334 VXNlcm5hbWU6")
                u = base64.b64decode(readline()).decode()
                send("334 UGFzc3dvcmQ6")
                p = base64.b64decode(readline()).decode()
                s.auth.append(("LOGIN", u, p))
                if u == USER and p == PASS:
                    send("235 2.7.0 tamam")
                else:
                    send("535 5.7.8 kimlik reddedildi")
            elif up.startswith("MAIL FROM:"):
                s.mail_from = line[10:]
                send("250 2.1.0 tamam")
            elif up.startswith("RCPT TO:"):
                if self.reject_rcpt and self.reject_rcpt in line:
                    send("550 5.1.1 boyle biri yok")
                else:
                    s.rcpts.append(line[8:])
                    send("250 2.1.5 tamam")
            elif up == "DATA":
                s.tls_at_data = tls
                send("354 bitirmek icin <CRLF>.<CRLF>")
                buf = []
                while True:
                    ln = f.readline(70000)
                    if not ln:
                        raise EOFError
                    if ln == b".\r\n":
                        break
                    if ln.startswith(b"."):
                        ln = ln[1:]  # RFC 5321 4.5.2 nokta geri alma
                    buf.append(ln)
                s.data = b"".join(buf)
                send("250 2.0.0 kuyruga alindi kimlik=42")
            elif up == "QUIT":
                send("221 2.0.0 gule gule")
                return
            else:
                send("502 5.5.2 bilinmeyen komut")


def gen_certs(d: str):
    def run(*a):
        subprocess.run(["openssl", *a], cwd=d, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "ca.key",
        "-out", "ca.pem", "-days", "2", "-subj", "/CN=Tulpar Test CA",
        "-addext", "basicConstraints=critical,CA:TRUE",
        "-addext", "keyUsage=critical,keyCertSign,cRLSign")
    for name in ("localhost", "baska.example"):
        run("req", "-newkey", "rsa:2048", "-nodes", "-keyout", f"{name}.key",
            "-out", f"{name}.csr", "-subj", f"/CN={name}")
        with open(os.path.join(d, f"{name}.ext"), "w") as fh:
            fh.write(f"subjectAltName=DNS:{name}\n")
        run("x509", "-req", "-in", f"{name}.csr", "-CA", "ca.pem", "-CAkey",
            "ca.key", "-CAcreateserial", "-out", f"{name}.pem", "-days", "2",
            "-extfile", f"{name}.ext")
    run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "self.key",
        "-out", "self.pem", "-days", "2", "-subj", "/CN=localhost",
        "-addext", "subjectAltName=DNS:localhost")


def ctx_for(d: str, name: str) -> ssl.SSLContext:
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(os.path.join(d, f"{name}.pem"), os.path.join(d, f"{name}.key"))
    return ctx


PROGRAM = r'''
import "smtp";

func cfg(str port, str sec, str user, str pw) {
    json c = {"host": "localhost", "port": toInt(env(port)), "security": sec,
              "timeout_ms": 5000};
    if (len(user) > 0) {
        c["user"] = user;
        c["pass"] = pw;
    }
    return c;
}

func show(str label, json r) {
    print(label + " ok=" + toString(r["ok"]) + " code=" + toString(r["code"]) +
          " stage=" + toString(r["stage"]) + " err=" + toString(r["error"]));
}

json msg = {"from": "Hamza <gonderen@example.com>", "to": "alici@example.com",
            "reply_to": "ziyaretci@example.com\r\nBcc: kurban@example.com",
            "subject": "[Portfolio İletişim] Şükrü'nün çok uzun, ğüşiöç dolu bir konusu — katlanmalı mı?",
            "html": "<p>Merhaba</p>\n.\n..iki nokta\n.son"};

show("S1", smtp_mail(cfg("P_STARTTLS", "starttls", "gonderen@example.com", "gizli-parola-123"), msg));
show("S2", smtp_mail(cfg("P_LOGIN", "starttls", "gonderen@example.com", "gizli-parola-123"), msg));
show("S3", smtp_mail(cfg("P_TLS", "tls", "gonderen@example.com", "gizli-parola-123"), msg));
show("S4", smtp_mail(cfg("P_WRONGNAME", "starttls", "gonderen@example.com", "gizli-parola-123"), msg));
show("S5", smtp_mail(cfg("P_SELF", "tls", "gonderen@example.com", "gizli-parola-123"), msg));
show("S6", smtp_mail(cfg("P_NOSTARTTLS", "starttls", "gonderen@example.com", "gizli-parola-123"), msg));
show("S7", smtp_mail(cfg("P_INJECT", "starttls", "gonderen@example.com", "gizli-parola-123"), msg));
show("S8", smtp_mail(cfg("P_BADPASS", "starttls", "gonderen@example.com", "yanlis"), msg));
json c9 = cfg("P_STARTTLS", "none", "gonderen@example.com", "gizli-parola-123");
c9["host"] = "127.0.0.2";
show("S9", smtp_mail(c9, msg));
json m10 = {"from": "gonderen@example.com", "to": ["alici@example.com", "yok@example.com"],
            "subject": "x", "text": "y"};
show("S10", smtp_mail(cfg("P_RCPT", "starttls", "gonderen@example.com", "gizli-parola-123"), m10));
json m11 = {"from": "gonderen@example.com", "to": "a@b.c>\r\nRCPT TO:<kurban@example.com",
            "subject": "x", "text": "y"};
show("S11", smtp_mail(cfg("P_STARTTLS", "starttls", "", ""), m11));
// Ham ileti: nokta doldurma yalniz burada sinaniyor (smtp_message govdesi
// base64, icinde "." ile baslayan satir hic olmuyor). Doldurulmazsa sunucu
// ".\r\n" satirinda iletiyi KESER ve kalan satirlari komut sanar.
str raw = "From: gonderen@example.com\nTo: alici@example.com\nSubject: ham\n\nbir\n.\n..iki\n.uc\nson";
show("S12", smtp_send(cfg("P_RAW", "starttls", "gonderen@example.com", "gizli-parola-123"),
                      "gonderen@example.com", "alici@example.com", raw));
'''


def main() -> int:
    if not shutil.which("openssl"):
        print("SKIP: openssl yok (sertifika uretilemez)")
        return 0
    exe = find_tulpar_exe()
    d = tempfile.mkdtemp(prefix="tulpar_smtp_")
    try:
        gen_certs(d)
        good = ctx_for(d, "localhost")
        servers = {
            "P_STARTTLS": Sunucu(good, "starttls"),
            "P_LOGIN": Sunucu(good, "starttls", auth="login"),
            "P_TLS": Sunucu(good, "tls"),
            "P_WRONGNAME": Sunucu(ctx_for(d, "baska.example"), "starttls"),
            "P_SELF": Sunucu(ctx_for(d, "self"), "tls"),
            "P_NOSTARTTLS": Sunucu(good, "plain-only"),
            "P_INJECT": Sunucu(good, "inject"),
            "P_BADPASS": Sunucu(good, "starttls"),
            "P_RCPT": Sunucu(good, "starttls", reject_rcpt="yok@"),
            "P_RAW": Sunucu(good, "starttls"),
        }
        src = os.path.join(d, "gonder.tpr")
        with open(src, "w") as fh:
            fh.write(PROGRAM)
        env = dict(os.environ)
        env["TULPAR_CA_BUNDLE"] = os.path.join(d, "ca.pem")
        env.pop("TULPAR_TLS_INSECURE", None)
        env["DISPLAY"] = ""
        for k, s in servers.items():
            env[k] = str(s.port)
        p = subprocess.run([exe, src], env=env, capture_output=True, text=True,
                           timeout=120)
        out = p.stdout + p.stderr
        if p.returncode != 0:
            if "OpenSSL yok" in out:
                print("SKIP: ikili TLS'siz derlenmis")
                return 0
            print(out)
            print(f"FAIL: program cikis kodu {p.returncode}")
            return 1
        res = {}
        for ln in p.stdout.splitlines():
            if ln.startswith("S") and " ok=" in ln:
                res[ln.split(" ")[0]] = ln
        time.sleep(0.2)

        fails = []

        def check(cond, why):
            if not cond:
                fails.append(why)

        def ok(tag):
            return f"{tag} ok=1" in res.get(tag, "")

        def sess(k) -> Oturum | None:
            ss = servers[k].sessions
            return ss[0] if ss else None

        # --- basarili gonderimler: sunucu tarafinda dogrula
        for tag, k, mech in (("S1", "P_STARTTLS", "PLAIN"), ("S2", "P_LOGIN", "LOGIN"),
                             ("S3", "P_TLS", "PLAIN")):
            check(ok(tag), f"{tag}: gonderim basarisiz: {res.get(tag)}")
            s = sess(k)
            if s is None or s.data is None:
                fails.append(f"{tag}: sunucu ileti almadi ({s.commands if s else 'baglanti yok'})")
                continue
            check(s.tls_at_auth is True, f"{tag}: AUTH TLS altinda degil")
            check(s.tls_at_data is True, f"{tag}: DATA TLS altinda degil")
            check(s.auth == [(mech, USER, PASS)], f"{tag}: kimlik {s.auth}")
            check(s.mail_from == "<gonderen@example.com>", f"{tag}: MAIL FROM {s.mail_from}")
            check(s.rcpts == ["<alici@example.com>"], f"{tag}: RCPT {s.rcpts}")
            m = email.message_from_bytes(s.data, policy=email.policy.default)
            subj = str(m["Subject"])
            check(subj == "[Portfolio İletişim] Şükrü'nün çok uzun, ğüşiöç dolu bir konusu — katlanmalı mı?",
                  f"{tag}: konu geri cozulemedi: {subj!r}")
            check(m["Bcc"] is None, f"{tag}: Reply-To CR/LF yeni baslik dogurdu")
            hdr = s.data.split(b"\r\n\r\n", 1)[0].split(b"\r\n")
            check(b"Reply-To: ziyaretci@example.com  Bcc: kurban@example.com" in hdr,
                  f"{tag}: Reply-To satiri {[h for h in hdr if h.startswith(b'Reply-To')]}")
            check(not any(h.startswith(b"Bcc") for h in hdr), f"{tag}: Bcc satiri var")
            check(m.get_content_type() == "text/html", f"{tag}: tur {m.get_content_type()}")
            check(m.get_content_charset() == "utf-8", f"{tag}: charset {m.get_content_charset()}")
            body = m.get_content()
            check(body.replace("\r\n", "\n") == "<p>Merhaba</p>\n.\n..iki nokta\n.son",
                  f"{tag}: govde {body!r}")
            check(m["Date"] is not None and m["Message-ID"] is not None, f"{tag}: Date/Message-ID yok")
            check(b"\n" not in s.data.replace(b"\r\n", b""), f"{tag}: ciplak LF")
            for raw_ln in s.data.split(b"\r\n"):
                check(len(raw_ln) <= 998, f"{tag}: 998'i asan satir")

        # --- reddedilmesi gerekenler: istemci ok=0, sunucu AUTH/MAIL gormemis
        for tag, k, stage in (("S4", "P_WRONGNAME", "starttls"), ("S5", "P_SELF", "connect"),
                              ("S6", "P_NOSTARTTLS", "starttls"), ("S7", "P_INJECT", "starttls")):
            check(f"{tag} ok=0" in res.get(tag, ""), f"{tag}: REDDEDILMELIYDI: {res.get(tag)}")
            check(f"stage={stage}" in res.get(tag, ""), f"{tag}: asama: {res.get(tag)}")
            s = sess(k)
            if s is not None:
                check(not s.auth, f"{tag}: parola gonderildi! {s.auth}")
                check(s.mail_from is None, f"{tag}: MAIL gonderildi")
        check("cert verify" in res.get("S4", ""), f"S4: hata nedeni {res.get('S4')}")
        check("cert verify" in res.get("S5", ""), f"S5: hata nedeni {res.get('S5')}")

        check("S8 ok=0 code=535 stage=auth" in res.get("S8", ""), f"S8: {res.get('S8')}")
        s8 = sess("P_BADPASS")
        check(s8 is not None and s8.mail_from is None, "S8: yanlis paroladan sonra MAIL gitti")
        check("S9 ok=0" in res.get("S9", "") and "stage=config" in res.get("S9", ""),
              f"S9: TLS'siz parola reddedilmedi: {res.get('S9')}")
        check("S10 ok=0 code=550 stage=rcpt" in res.get("S10", ""), f"S10: {res.get('S10')}")
        s10 = sess("P_RCPT")
        check(s10 is not None and s10.data is None, "S10: RCPT reddine ragmen DATA gitti")
        check("S11 ok=0" in res.get("S11", "") and "stage=config" in res.get("S11", ""),
              f"S11: zarf enjeksiyonu reddedilmedi: {res.get('S11')}")
        # S11 yalniz config asamasinda dusmeli: P_STARTTLS'e ikinci baglanti olmamali
        check(len(servers["P_STARTTLS"].sessions) == 1,
              f"S11: zarf enjeksiyonunda yine de baglanildi ({len(servers['P_STARTTLS'].sessions)} oturum)")

        check(ok("S12"), f"S12: {res.get('S12')}")
        s12 = sess("P_RAW")
        check(s12 is not None and s12.data is not None and
              s12.data.split(b"\r\n\r\n", 1)[-1] == b"bir\r\n.\r\n..iki\r\n.uc\r\nson\r\n",
              f"S12: nokta doldurma bozuk: {s12.data if s12 else None!r}")
        check(s12 is not None and all(not c.startswith("..") and c not in (".uc", "son")
                                      for c in s12.commands),
              f"S12: govde komut olarak yorumlandi: {s12.commands if s12 else None}")

        if fails:
            print(p.stdout)
            for f in fails:
                print("  - " + f)
            print(f"FAIL: smtp_smoke {len(fails)} kontrol kirmizi")
            return 1
        print("PASS: smtp_smoke — 3 gonderim (STARTTLS+PLAIN, STARTTLS+LOGIN, TLS) "
              "sunucuda dogrulandi; yanlis ad / CA disi / indirgeme / enjeksiyon / "
              "yanlis parola / RCPT reddi / TLS'siz parola / zarf enjeksiyonu reddedildi; ham iletide nokta doldurma")
        return 0
    finally:
        shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
