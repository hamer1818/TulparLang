#!/usr/bin/env python3
"""`tulpar debug` (DAP) denetimi — penceresiz, stdin/stdout JSON üzerinden.

NEDEN
-----
Hata ayıklayıcının HİÇBİR denetimi yoktu ve kaynak yorumu "All DAP handlers
are now fully implemented" diyordu. Ölçüldü (2026-09-27): gdb
`*stopped,reason="breakpoint-hit"` ve `*stopped,reason="exited-normally"`
kayıtlarını gönderiyor, ama bağdaştırıcı HİÇ `stopped` / `terminated` olayı
üretmiyordu. Sebep okuyucu iş parçacığında: kayıt `'*'` önekiyle
iletiliyor, `"stopped"` karşılaştırması hiç tutmuyordu (depo geçmişinin
başından beri). VS Code'da breakpoint'te duraklama görünmüyor, oturum program
bitince kapanmıyor, logpoint'ler sessiz bir duraklamaya dönüşüyordu —
stackTrace/variables istekleri ise çalıştığı için elle sonda "çalışıyor"
diyordu.

NE ÖLÇÜLÜYOR (üç senaryo, her biri ayrı `tulpar debug` süreci)
  1. breakpoint: initialize -> launch -> setBreakpoints(verified) ->
     configurationDone -> `stopped(reason=breakpoint)` -> stackTrace'in üst
     çerçevesi breakpoint SATIRINDA -> scopes -> variables yerel DEĞERLERİ
     taşıyor -> continue -> `exited(exitCode=0)` + `terminated`.
  2. koşullu breakpoint (`i == 3`, döngüde): TAM BİR kez durur ve `i` = 3.
  3. logpoint (`i={i}`): hiç `stopped` YOK, beş `output` olayı i=0..4 ile,
     sonra `terminated`.

POZİTİF KONTROL: 1. senaryodaki değişken değerleri programın kendi
hesabından (a=2, b=3, c=5) geliyor, bağdaştırıcının bir sabitinden değil;
2. senaryo koşulsuz aynı breakpoint'in BEŞ kez durduğunu da ölçüyor — yani
"bir kez durdu" koşulun eseri, erken çıkışın değil.

gdb YOKSA (macOS CI, MSYS2) GÖRÜNÜR atlanır: son satır `SKIP ...`.
"""
import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TIMEOUT = 60.0  # launch bir AOT derlemesi yapıyor; CI yavaş olabilir

PROG_FUNC = (
    "func topla(int a, int b): int {\n"   # 1
    "    int c = a + b;\n"                 # 2
    "    return c;\n"                      # 3  <- breakpoint
    "}\n"                                  # 4
    "int r = topla(2, 3);\n"               # 5
    "print(r);\n"                          # 6
)
PROG_LOOP = (
    "func adim(int i): int {\n"            # 1
    "    int k = i * 10;\n"                # 2  <- breakpoint / logpoint
    "    return k;\n"                      # 3
    "}\n"                                  # 4
    "int t = 0;\n"                         # 5
    "for (int i = 0; i < 5; i = i + 1) {\n"  # 6
    "    t = t + adim(i);\n"               # 7
    "}\n"                                  # 8
    "print(t);\n"                          # 9
)


def frame(obj):
    body = json.dumps(obj).encode("utf-8")
    return b"Content-Length: %d\r\n\r\n%s" % (len(body), body)


class Adapter:
    def __init__(self, exe, prog, workdir, env=None):
        self.err = open(os.path.join(workdir, "dap.err"), "w")
        self.proc = subprocess.Popen([exe, "debug", prog], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=self.err,
                                     cwd=workdir, env=env)
        self.q = queue.Queue()
        self.seq = 0
        self.log = []
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        out = self.proc.stdout
        while True:
            hdr = b""
            while not hdr.endswith(b"\r\n\r\n"):
                c = out.read(1)
                if not c:
                    self.q.put(None)
                    return
                hdr += c
            n = 0
            for ln in hdr.decode("ascii", "replace").split("\r\n"):
                if ln.lower().startswith("content-length:"):
                    n = int(ln.split(":", 1)[1])
            try:
                self.q.put(json.loads(out.read(n)))
            except Exception:
                self.q.put({"type": "garbled"})

    def request(self, cmd, args=None):
        self.seq += 1
        m = {"seq": self.seq, "type": "request", "command": cmd}
        if args is not None:
            m["arguments"] = args
        self.proc.stdin.write(frame(m))
        self.proc.stdin.flush()
        return self.seq

    def next_msg(self, deadline):
        left = deadline - time.time()
        if left <= 0:
            return None
        try:
            m = self.q.get(timeout=left)
        except queue.Empty:
            return None
        if m is not None:
            self.log.append(m)
        return m

    def wait(self, pred, what, timeout=TIMEOUT):
        """pred'i sağlayan ilk mesajı döndürür; gelmezse AssertionError."""
        deadline = time.time() + timeout
        while True:
            m = self.next_msg(deadline)
            if m is None:
                raise AssertionError("%s bekleniyordu, gelmedi (%.0f sn)" % (what, timeout))
            if pred(m):
                return m

    def call(self, cmd, args=None, timeout=TIMEOUT):
        s = self.request(cmd, args)
        r = self.wait(lambda m: m.get("type") == "response" and m.get("request_seq") == s,
                      "'%s' cevabı" % cmd, timeout)
        if not r.get("success"):
            raise AssertionError("'%s' başarısız: %s" % (cmd, r.get("message")))
        return r

    def close(self):
        try:
            if self.proc.poll() is None:
                self.request("disconnect", {})
                self.proc.wait(5)
        except Exception:
            pass
        if self.proc.poll() is None:
            self.proc.kill()
        self.err.close()


def is_event(name):
    return lambda m: m.get("type") == "event" and m.get("event") == name


def start(exe, work, src_text, bps):
    prog = os.path.join(work, "prog.tpr")
    with open(prog, "w") as fh:
        fh.write(src_text)
    a = Adapter(exe, prog, work)
    a.call("initialize", {"adapterID": "tulpar", "linesStartAt1": True})
    a.call("launch", {"program": prog})
    r = a.call("setBreakpoints", {"source": {"path": prog}, "breakpoints": bps})
    got = r.get("body", {}).get("breakpoints", [])
    if len(got) != len(bps) or not all(b.get("verified") for b in got):
        raise AssertionError("setBreakpoints doğrulanmadı: %s" % got)
    a.call("configurationDone")
    return a


def locals_of(a):
    st = a.call("stackTrace", {"threadId": 1})
    frames = st["body"]["stackFrames"]
    if not frames:
        raise AssertionError("stackTrace boş")
    sc = a.call("scopes", {"frameId": frames[0]["id"]})
    ref = sc["body"]["scopes"][0]["variablesReference"]
    vs = a.call("variables", {"variablesReference": ref})
    return frames, {v["name"]: v["value"] for v in vs["body"]["variables"]}


def run_to_exit(a, stops_allowed=0):
    """continue'leri sürer; `exited` + `terminated` bekler. Durma sayısını döndürür."""
    stops = 0
    exit_code = None
    while True:
        m = a.wait(lambda m: m.get("type") == "event" and
                   m.get("event") in ("stopped", "exited", "terminated"),
                   "stopped/exited/terminated")
        ev = m["event"]
        if ev == "stopped":
            stops += 1
            if stops > stops_allowed:
                return stops, exit_code, m
            a.call("continue", {"threadId": 1})
        elif ev == "exited":
            exit_code = m.get("body", {}).get("exitCode")
        else:
            return stops, exit_code, None


def scenario_breakpoint(exe, work):
    a = start(exe, work, PROG_FUNC, [{"line": 3}])
    try:
        st = a.wait(is_event("stopped"), "breakpoint'te `stopped` olayı")
        reason = st.get("body", {}).get("reason")
        if reason != "breakpoint":
            raise AssertionError("stopped.reason=%r (beklenen 'breakpoint')" % reason)
        frames, vars_ = locals_of(a)
        top = frames[0]
        if top.get("line") != 3 or top.get("name") not in ("topla", "t_topla"):
            raise AssertionError("üst çerçeve %s:%s (beklenen topla:3)" %
                                 (top.get("name"), top.get("line")))
        want = {"a": "2", "b": "3", "c": "5"}
        for k, v in want.items():
            if vars_.get(k) != v:
                raise AssertionError("değişken %s=%r (beklenen %s); hepsi: %s" %
                                     (k, vars_.get(k), v, vars_))
        a.call("continue", {"threadId": 1})
        stops, code, _ = run_to_exit(a)
        if code != 0:
            raise AssertionError("`exited` olayı exitCode=%r (beklenen 0)" % code)
        return "breakpoint: stopped(breakpoint) topla:3, a=2 b=3 c=5, exited(0)+terminated"
    finally:
        a.close()


def scenario_conditional(exe, work):
    # Koşulsuz: beş kez durmalı (pozitif kontrol — döngü gerçekten beş tur).
    a = start(exe, work, PROG_LOOP, [{"line": 2}])
    try:
        stops, _, _ = run_to_exit(a, stops_allowed=99)
    finally:
        a.close()
    if stops != 5:
        raise AssertionError("koşulsuz breakpoint %d kez durdu (beklenen 5)" % stops)
    a = start(exe, work, PROG_LOOP, [{"line": 2, "condition": "i == 3"}])
    try:
        a.wait(is_event("stopped"), "koşullu breakpoint'te `stopped`")
        _, vars_ = locals_of(a)
        if vars_.get("i") != "3":
            raise AssertionError("koşullu durmada i=%r (beklenen 3)" % vars_.get("i"))
        a.call("continue", {"threadId": 1})
        stops, _, _ = run_to_exit(a, stops_allowed=99)
        if stops != 0:
            raise AssertionError("koşullu breakpoint ikinci kez durdu (%d)" % stops)
        return "koşullu: koşulsuz 5 durma (kontrol), `i == 3` tam 1 durma, i=3"
    finally:
        a.close()


def scenario_logpoint(exe, work):
    a = start(exe, work, PROG_LOOP, [{"line": 2, "logMessage": "LOG i={i}"}])
    try:
        stops, _, _ = run_to_exit(a, stops_allowed=0)
        if stops:
            raise AssertionError("logpoint DURDURDU (%d stopped) — çıktı + devam bekleniyordu" % stops)
        outs = "".join(m.get("body", {}).get("output", "") for m in a.log
                       if m.get("type") == "event" and m.get("event") == "output")
        lines = [ln for ln in outs.splitlines() if ln.startswith("LOG i=")]
        want = ["LOG i=%d" % i for i in range(5)]
        if lines != want:
            raise AssertionError("logpoint çıktısı %s (beklenen %s)" % (lines, want))
        return "logpoint: 0 durma, 5 output (i=0..4), terminated"
    finally:
        a.close()


def scenario_no_gdb_hint(exe, work):
    """gdb PATH'te yoksa `launch` NET bir kurulum ipucuyla düşmeli (K222).

    Eskiden POSIX'te fork başarılı olduğu için launch "ok" dönüyor, oturum
    ilk -break-insert'te zaman aşımıyla ölüyordu; Windows'ta CreateProcess
    çıplak bir hata koduyla düşüyordu. gdb'den BAĞIMSIZ koşar (macOS'ta da).
    """
    prog = os.path.join(work, "prog.tpr")
    with open(prog, "w") as fh:
        fh.write(PROG_FUNC)
    empty = os.path.join(work, "bos_path")
    os.mkdir(empty)
    env = dict(os.environ)
    env["PATH"] = empty
    env["LC_ALL"] = "C"
    a = Adapter(exe, prog, work, env=env)
    try:
        a.call("initialize", {"adapterID": "tulpar", "linesStartAt1": True})
        s = a.request("launch", {"program": prog})
        r = a.wait(lambda m: m.get("type") == "response" and m.get("request_seq") == s,
                   "'launch' cevabı")
        msg = r.get("message") or ""
        if r.get("success"):
            raise AssertionError("gdb yokken launch BAŞARILI döndü")
        if "gdb" not in msg or not any(k in msg for k in ("pacman", "apt", "brew")):
            raise AssertionError("launch hata mesajı kurulum ipucu taşımıyor: %r" % msg)
        return "gdb yok: launch net ipucuyla dustu (%s)" % msg.split(";")[0][:60]
    finally:
        a.close()


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "tulpar")
    if not os.path.isfile(exe) and os.path.isfile(exe + ".exe"):
        exe += ".exe"
    exe = os.path.abspath(exe)
    failed = False
    work = tempfile.mkdtemp(prefix="tulpar_dap_")
    try:
        print("  gecti  " + scenario_no_gdb_hint(exe, work))
    except AssertionError as e:
        failed = True
        print("  DUSTU  scenario_no_gdb_hint: %s" % e)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if not shutil.which("gdb"):
        if failed:
            print("dap denetimi DUSTU")
            return 1
        if os.environ.get("TULPAR_DAP_ZORUNLU") == "1":
            print("dap denetimi DUSTU: gdb yok ama TULPAR_DAP_ZORUNLU=1 (Linux CI) — "
                  "atlama burada kabul edilmiyor")
            return 1
        print("SKIP dap denetimi: gdb yok (bagdastirici gdb MI3 kopru)")
        return 0
    for fn in (scenario_breakpoint, scenario_conditional, scenario_logpoint):
        work = tempfile.mkdtemp(prefix="tulpar_dap_")
        try:
            print("  gecti  " + fn(exe, work))
        except AssertionError as e:
            failed = True
            print("  DUSTU  %s: %s" % (fn.__name__, e))
            try:
                with open(os.path.join(work, "dap.err")) as fh:
                    tail = fh.read().splitlines()[-8:]
                for ln in tail:
                    print("      " + ln)
            except OSError:
                pass
        finally:
            shutil.rmtree(work, ignore_errors=True)
    if failed:
        print("dap denetimi DUSTU")
        return 1
    print("dap denetimi temiz (breakpoint + kosullu + logpoint; stopped/exited/terminated olaylari)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
