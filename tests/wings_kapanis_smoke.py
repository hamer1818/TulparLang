"""SIGTERM'de duzgun kapanis (graceful_shutdown + on_shutdown) ve hamza-backend
komut satiri tarifi (set-password stdin'den, backup = VACUUM INTO) —
TULPAR_ISLERI.md B12. Gercek surec, gercek sinyal.

Olculenler (listen_pool VE tek iplikli listen icin ayri ayri):
  1. 1,5 sn suren bir istek surerken SIGTERM: istek 200 ile TAMAMLANIR,
     surec 0 ile cikar, on_shutdown kancasi kosar (dosyaya yazar).
  2. Bosta acik duran keep-alive baglantisi kapanisi bekletmez (pool).
  3. Sure siniri: 10 sn'lik istek + graceful_shutdown(1) -> ~1 sn'de cikis 0.
  4. graceful_shutdown cagrilmadiysa davranis DEGISMEDI: SIGTERM sureci
     hemen oldurur (-15).
  5. set-password: parola stdin'den, bcrypt $2b$12$, kisa parola ret;
     ON CONFLICT ile guncelleme. backup: VACUUM INTO tutarli kopya, var olan
     dosyaya yazmayi reddeder.
"""

from __future__ import annotations

import http.client
import os
import signal
import socket
import sqlite3
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


SERVER = r'''
import "wings";

func slow() {
    sleep(toInt(env("SLOW_MS")));
    return {"done": 1};
}
func ping() {
    return {"ok": 1};
}
func hook() {
    write_file(env("HOOK_FILE"), "kanca kostu");
}

get("/slow", "slow");
get("/ping", "ping");
on_shutdown("hook");
if (toInt(env("GRACE")) > 0) {
    graceful_shutdown(toInt(env("GRACE")));
}
if (env("MODE") == "pool") {
    listen_pool(toInt(env("SMOKE_PORT")), 4);
} else {
    listen(toInt(env("SMOKE_PORT")));
}
'''

CLI = r'''
// hamza-backend `server set-password <email>` / `server backup <file>` tarifi.
json a = args();
int db = db_open(env("DB"));
db_execute(db, "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, email TEXT UNIQUE NOT NULL, password TEXT NOT NULL)");
if (length(a) > 2 && a[1] == "set-password") {
    str pw = input("");
    pw = replace(replace(pw, "\r", ""), "\n", "");
    if (length(pw) < 12) {
        print("Password must be at least 12 characters");
        exit(1);
    }
    bool ok = db_execute(db, "INSERT INTO users (email, password) VALUES (?, ?) ON CONFLICT (email) DO UPDATE SET password = excluded.password",
                         [a[2], bcrypt_hash(pw, 12)]);
    if (!ok) {
        print("Could not update password: " + db_error(db));
        exit(1);
    }
    print("Password updated for " + a[2]);
    exit(0);
}
if (length(a) > 2 && a[1] == "backup") {
    if (!db_execute(db, "VACUUM INTO ?", [a[2]])) {
        print("Backup failed: " + db_error(db));
        exit(1);
    }
    print("Backup written to " + a[2]);
    exit(0);
}
print("usage");
exit(2);
'''


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def get(port, path, timeout=20):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    c.request("GET", path, headers={"Connection": "close"})
    r = c.getresponse()
    out = (r.status, r.read())
    c.close()
    return out


def build(exe, d, name, src):
    p = os.path.join(d, name + ".tpr")
    with open(p, "w") as fh:
        fh.write(src)
    b = os.path.join(d, name)
    r = subprocess.run([exe, "build", p, b], capture_output=True, text=True, env=dict(os.environ, DISPLAY=""))
    if r.returncode != 0:
        raise SystemExit(r.stdout + r.stderr + "\nFAIL: derlenmedi: " + name)
    return b


def start(binp, mode, grace, slow_ms, hook):
    port = free_port()
    env = dict(os.environ, SMOKE_PORT=str(port), MODE=mode, GRACE=str(grace), SLOW_MS=str(slow_ms),
               HOOK_FILE=hook, TULPAR_HTTP_QUIET="1", TULPAR_WINGS_NODOCS="1")
    p = subprocess.Popen([binp], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for _ in range(100):
        try:
            if get(port, "/ping", 1)[0] == 200:
                return p, port
        except OSError:
            time.sleep(0.05)
    p.kill()
    raise SystemExit("FAIL: sunucu acilmadi")


def main() -> int:
    if sys.platform in ("win32", "cygwin", "msys"):
        print("SKIP: Windows'ta SIGTERM yok (graceful_shutdown POSIX sinyali bekliyor)")
        return 0
    exe = find_tulpar_exe()
    d = tempfile.mkdtemp(prefix="tulpar_kapanis_")
    srv = build(exe, d, "srv", SERVER)
    cli = build(exe, d, "cli", CLI)
    fails = []

    def check(cond, why):
        if not cond:
            fails.append(why)

    for mode in ("pool", "single"):
        # 1. Suren istek tamamlanir, kanca kosar, cikis 0
        hook = os.path.join(d, f"hook_{mode}")
        p, port = start(srv, mode, 8, 1500, hook)
        idle = None
        if mode == "pool":
            # 2. Bosta keep-alive baglantisi (istek yapip acik birak)
            idle = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
            idle.request("GET", "/ping")
            idle.getresponse().read()
        res = {}
        th = threading.Thread(target=lambda: res.setdefault("r", get(port, "/slow")))
        th.start()
        time.sleep(0.4)
        t0 = time.time()
        p.send_signal(signal.SIGTERM)
        th.join(10)
        try:
            rc = p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()
            rc = "zaman asimi"
        took = time.time() - t0
        out = p.stdout.read().decode(errors="replace")
        r = res.get("r")
        check(r is not None and r[0] == 200 and b"done" in r[1], f"{mode}: suren istek kesildi: {r}")
        check(rc == 0, f"{mode}: cikis kodu {rc} (cikti: {out[-300:]!r})")
        check(os.path.exists(hook) and open(hook).read() == "kanca kostu", f"{mode}: on_shutdown kosmadi")
        limit = 4.5 if mode == "pool" else 5.0
        check(took < limit, f"{mode}: kapanis {took:.1f} sn surdu (bosta keep-alive bekletti mi?)")
        check("Server stopped" not in out or "cut off" not in out, f"{mode}: istek kesildi dendi: {out!r}")
        if idle:
            idle.close()

        # 3. Sure siniri
        hook3 = os.path.join(d, f"hook3_{mode}")
        p, port = start(srv, mode, 1, 10000, hook3)
        th = threading.Thread(target=lambda: _swallow(lambda: get(port, "/slow", 15)))
        th.start()
        time.sleep(0.4)
        t0 = time.time()
        p.send_signal(signal.SIGTERM)
        try:
            rc = p.wait(8)
        except subprocess.TimeoutExpired:
            p.kill()
            rc = "zaman asimi"
        took = time.time() - t0
        check(rc == 0 and took < 4.0, f"{mode}: sure siniri tutmadi rc={rc} {took:.1f} sn")
        check(os.path.exists(hook3), f"{mode}: sure sinirinda kanca kosmadi")

        # 4. graceful_shutdown yoksa eski davranis
        p, port = start(srv, mode, 0, 10, os.path.join(d, "hook4"))
        p.send_signal(signal.SIGTERM)
        try:
            rc = p.wait(5)
        except subprocess.TimeoutExpired:
            p.kill()
            rc = "zaman asimi"
        check(rc == -signal.SIGTERM, f"{mode}: opt-in degil — SIGTERM sureci oldurmedi: rc={rc}")
        check(not os.path.exists(os.path.join(d, "hook4")), f"{mode}: kanca opt-in olmadan kostu")

    # 5. set-password + backup
    db = os.path.join(d, "app.db")
    env = dict(os.environ, DB=db)
    r = subprocess.run([cli, "set-password", "admin@example.com"], input="kisa\n", env=env,
                       capture_output=True, text=True)
    check(r.returncode == 1 and "at least 12" in r.stdout, f"cli: kisa parola: {r.returncode} {r.stdout!r}")
    r = subprocess.run([cli, "set-password", "admin@example.com"], input="cok-guclu-parola-1\n", env=env,
                       capture_output=True, text=True)
    check(r.returncode == 0, f"cli: set-password {r.returncode} {r.stdout!r}")
    h1 = sqlite3.connect(db).execute("SELECT password FROM users WHERE email='admin@example.com'").fetchone()
    check(h1 is not None and h1[0].startswith("$2b$12$") and len(h1[0]) == 60, f"cli: hash {h1}")
    r = subprocess.run([cli, "set-password", "admin@example.com"], input="baska-guclu-parola-2\r\n", env=env,
                       capture_output=True, text=True)
    rows = sqlite3.connect(db).execute("SELECT password FROM users").fetchall()
    check(r.returncode == 0 and len(rows) == 1 and rows[0][0] != h1[0], f"cli: guncelleme {rows}")
    bk = os.path.join(d, "yedek.db")
    r = subprocess.run([cli, "backup", bk], env=env, capture_output=True, text=True)
    check(r.returncode == 0 and os.path.exists(bk), f"cli: backup {r.stdout!r}")
    if os.path.exists(bk):
        rows2 = sqlite3.connect(bk).execute("SELECT password FROM users").fetchall()
        check(rows2 == rows, "cli: yedek icerigi farkli")
    r = subprocess.run([cli, "backup", bk], env=env, capture_output=True, text=True)
    check(r.returncode == 1 and "already exists" in r.stdout, f"cli: var olan yedege yazdi: {r.stdout!r}")

    if fails:
        for f in fails:
            print("  - " + f)
        print(f"FAIL: wings_kapanis_smoke {len(fails)} kontrol kirmizi")
        return 1
    print("PASS: wings_kapanis_smoke — SIGTERM'de suren istek bitiyor + kanca + cikis 0 "
          "(pool ve tek iplik), bosta keep-alive bekletmiyor, sure siniri tutuyor, "
          "opt-in degilse eski davranis; set-password/backup tarifi")
    return 0


def _swallow(fn):
    try:
        fn()
    except Exception:
        pass


if __name__ == "__main__":
    sys.exit(main())
