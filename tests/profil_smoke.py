#!/usr/bin/env python3
"""PROFILLEYICI KAPISI (`tulpar --profile`, 2026-11-26).

Profilin SAYILARI dogru mu — sure degil (makineye bagli), sayim ve oranlar:
  * fib(20) cagri sayisi tam 21891 (= 2*fib(21)-1); ozyineleme klonu `fib.recN`
    ayri satir degil, kapsayici sure ozyinelemede iki kez sayilmiyor
    (kapsayici <= ust duzey kapsayicisi);
  * tipsiz fonksiyon int ve str ile: dagitici + uzmanlasma TEK cagri (tam N);
  * yontem `Tip.ad` kendi adiyla;
  * throw eden fonksiyon: cagri sayisi tam, cikis kancasi atlansa da cerceve
    kapaniyor (cagiranin kapsayicisi >= cagrilaninki);
  * exit() ile biten program (test_summary kalibi) yine rapor basiyor;
  * thread: iscinin fonksiyonu sayiliyor, "2 iplik";
  * oz arena baytı: dizgi kuran fonksiyonda > 100 KB, saf int'te 0;
  * sure orani: 3x is yapan fonksiyonun oz suresi 1x olanin >= 2 kati;
  * folded cikti: her satir `a;b <us>`, ust duzey koku;
  * KONTROL: --profile olmadan rapor YOK ve ikilide aot_prof_enter referansi yok.
"""
import os
import re
import subprocess
import sys
import tempfile

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TUL = os.path.join(KOK, "tulpar")
fails = []


def check(ad, kosul, detay=""):
    print("  %s  %s %s" % ("gecti" if kosul else "DUSTU", ad, detay))
    if not kosul:
        fails.append(ad)


PROG = r'''
func fib(int n): int { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }
func kimlik(x) { return x; }
struct Kare { int k; }
func Kare.alan(): int { return self.k * self.k; }
func atar(int k) { if (k > 0) { throw "hata"; } return 1; }
func sarici() {
    int t = 0;
    for (int i = 0; i < 40; i++) { try { atar(i); } catch (e) { t = t + 1; } }
    return t;
}
func dizgici() {
    str s = "";
    for (int i = 0; i < 3000; i++) { s = s + toString(i); }
    return len(s);
}
func meşgul(int n): int {
    int t = 0;
    for (int i = 0; i < n; i++) { t = (t + i * 7) % 1000003; }
    return t;
}
func az() { return meşgul(3000000); }
func cok() { return meşgul(9000000); }
func isci(int x): int { return fib(15) + x; }
print(fib(20));
for (int i = 0; i < 10; i++) { kimlik(i); kimlik("s"); }
Kare q; q.k = 7;
print(q.alan());
print(sarici());
print(dizgici());
print(az() + cok());
int t = thread_create(isci, 1);
print(thread_join(t));
'''


def tablo(stderr):
    rows = {}
    for ln in stderr.splitlines():
        m = re.match(r"\s*([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s+([\d.]+)\s+(.+)$", ln)
        if m:
            rows[m.group(6).strip()] = {
                "oz_ms": float(m.group(2)), "kap_ms": float(m.group(3)),
                "cagri": int(m.group(4)), "kb": float(m.group(5))}
    return rows


def main():
    env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="", LC_ALL="C")
    with tempfile.TemporaryDirectory(prefix="profil_") as d:
        src = os.path.join(d, "p.tpr")
        open(src, "w", encoding="utf-8").write(PROG)
        folded = os.path.join(d, "p.folded")
        r = subprocess.run([TUL, "--profile", src], cwd=d, capture_output=True, text=True,
                           env=dict(env, TULPAR_PROFILE_OUT=folded), timeout=300)
        if r.returncode != 0:
            print("FAIL: profil_smoke — program dustu rc=%d\n%s" % (r.returncode, r.stderr[-1500:]))
            return 1
        rows = tablo(r.stderr)
        g = lambda ad, k: rows.get(ad, {}).get(k)
        check("fib(20) cagri sayisi tam", g("fib", "cagri") == 21891 + 1973,
              "(%s; 21891 ana + 1973 iscide fib(15))" % g("fib", "cagri"))
        check("ozyineleme klonu ayri satir degil", not any(".rec" in k for k in rows), str(list(rows)[:8]))
        check("kapsayici ozyinelemede iki kez sayilmiyor",
              g("fib", "kap_ms") is not None and g("fib", "kap_ms") <= g("<ust duzey>", "kap_ms") + 1e-3)
        check("dagitici + uzmanlasma tek cagri", g("kimlik", "cagri") == 20, "(%s)" % g("kimlik", "cagri"))
        check("yontem kendi adiyla", g("Kare.alan", "cagri") == 1, "(%s)" % g("Kare.alan", "cagri"))
        check("throw eden fonksiyon sayimi tam", g("atar", "cagri") == 40, "(%s)" % g("atar", "cagri"))
        check("throw sonrasi cerceve kapandi",
              g("sarici", "kap_ms") is not None and g("sarici", "kap_ms") + 1e-3 >= g("atar", "kap_ms"))
        check("thread iscisi sayildi", g("isci", "cagri") == 1 and "2 iplik" in r.stderr)
        check("oz arena: dizgici > 100 KB", (g("dizgici", "kb") or 0) > 100, "(%s KB)" % g("dizgici", "kb"))
        check("oz arena: saf int 0", g("meşgul", "kb") == 0.0)
        oz_az, oz_cok = g("az", "kap_ms"), g("cok", "kap_ms")
        check("sure orani 3x is >= 2x sure", oz_az and oz_cok and oz_cok >= 2.0 * oz_az,
              "(az %.2f ms, cok %.2f ms)" % (oz_az or 0, oz_cok or 0))
        lines = open(folded, encoding="utf-8").read().splitlines() if os.path.exists(folded) else []
        ok_fmt = lines and all(re.match(r"^\S.*\s\d+$", l) for l in lines)
        check("folded cikti bicimi", bool(ok_fmt), "(%d satir)" % len(lines))
        check("folded: ust duzey ve iplik kokleri, ic ice yol",
              any(l.startswith("<ust duzey>;fib;fib;") for l in lines) and any(l.startswith("isci;fib") for l in lines))

        # exit() ile biten program yine rapor basar.
        src2 = os.path.join(d, "x.tpr")
        open(src2, "w").write('func f(int n): int { return n + 1; }\nprint(f(1));\nexit(3);\n')
        r2 = subprocess.run([TUL, "--profile", src2], cwd=d, capture_output=True, text=True, env=env)
        check("exit() sonrasi rapor", r2.returncode == 3 and "tulpar profil" in r2.stderr and
              tablo(r2.stderr).get("f", {}).get("cagri") == 1, "(rc=%d)" % r2.returncode)

        # KONTROL: profilsiz ayni program — rapor yok, ikilide kanca yok.
        b = subprocess.run([TUL, "build", src, os.path.join(d, "duz")], cwd=d, capture_output=True,
                           text=True, env=env)
        r3 = subprocess.run([os.path.join(d, "duz")], cwd=d, capture_output=True, text=True, env=env)
        check("kontrol: profilsiz calismada rapor yok", "tulpar profil" not in r3.stderr and
              r3.stdout == r.stdout)
        b2 = subprocess.run([TUL, "build", "--profile", src, os.path.join(d, "prof")], cwd=d,
                            capture_output=True, text=True, env=env)
        def kanca_sayisi(ikili):
            dis = subprocess.run(["objdump", "-d", ikili], capture_output=True, text=True).stdout
            # x86-64 `call ... <aot_prof_enter>`, arm64 `bl ... <_aot_prof_enter>` (macOS).
            return len(re.findall(r"\b(call|callq|bl)\b.*<_?aot_prof_enter", dis))
        kp, kd = kanca_sayisi(os.path.join(d, "prof")), kanca_sayisi(os.path.join(d, "duz"))
        check("kontrol: build --profile ikilisinde kanca var, duz ikilide yok",
              b.returncode == 0 and b2.returncode == 0 and kp > 0 and kd == 0,
              "(profilli %d cagri yeri, duz %d)" % (kp, kd))
    if fails:
        print("FAIL: profil_smoke — %d dustu" % len(fails))
        return 1
    print("PASS: profil_smoke — sayimlar tam (ozyineleme, dagitici, yontem, throw, thread, exit), "
          "arena baytı, sure orani, folded; kontrol: profilsiz iz yok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
