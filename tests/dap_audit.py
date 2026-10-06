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

NE ÖLÇÜLÜYOR (yedi senaryo, her biri ayrı `tulpar debug` süreci)
  1. breakpoint: initialize -> launch -> setBreakpoints(verified) ->
     configurationDone -> `stopped(reason=breakpoint)` -> stackTrace'in üst
     çerçevesi breakpoint SATIRINDA -> scopes -> variables yerel DEĞERLERİ
     taşıyor -> continue -> `exited(exitCode=0)` + `terminated`.
  2. koşullu breakpoint (`i == 3`, döngüde): TAM BİR kez durur ve `i` = 3.
  3. logpoint (`i={i}`): hiç `stopped` YOK, beş `output` olayı i=0..4 ile,
     sonra `terminated`.
  4. okunur değerler (K153): gömülü VMValue pretty-printer'ı yüklü —
     `ad="Hamza"`, `f=2.5`, `a=[1, 2, 3]`, `j={"k": 7, ...}`, `b=true`; ve
     `tulpar debug --gdb-script` çıktısı düz gdb'de aynı değerleri veriyor.
  5. veri breakpoint'i (K151): global `t`'ye yazma izleyicisi —
     `dataBreakpointInfo` + `setDataBreakpoints(write)`, kaynak breakpoint
     silinince tam DÖRT `stopped(reason="data breakpoint")`, her birinde
     `evaluate t` sırayla 10, 30, 60, 100 (i=0 turu değeri değiştirmez).
  6. komut breakpoint'i (K151): `adim`'daki ilk durmanın
     `instructionPointerReference`'ı ile `setInstructionBreakpoints`,
     kaynak breakpoint silinince kalan dört çağrıda durur, `i` = 1..4.
  7. async coroutine'ler (K156): ayrı "thread" olarak görünüyor, bekleyenin
     stackTrace'i await zinciri (senaryonun kendi notuna bakın).

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
PROG_ASYNC = (
    "async func veri_oku(int n): int {\n"          # 1
    "    await sleep_async(50);\n"                  # 2
    "    return n;\n"                               # 3
    "}\n"                                           # 4
    "async func seviye_yukle(int n): int {\n"      # 5
    "    int v = await veri_oku(n);\n"              # 6
    "    return v * 10;\n"                          # 7
    "}\n"                                           # 8
    "async func hesapla(int x): int {\n"           # 9
    "    int y = x + 1;\n"                          # 10
    "    return y;\n"                               # 11 <- breakpoint
    "}\n"                                           # 12
    "var a = seviye_yukle(1);\n"                    # 13
    "var b = seviye_yukle(2);\n"                    # 14
    "int t = await hesapla(5);\n"                   # 15
    "t = t + await a + await b;\n"                  # 16
    "print(t);\n"                                   # 17
)

PROG_VALS = (
    "func goster(int n) {\n"                          # 1
    "    str ad = \"Hamza\";\n"                        # 2
    "    float f = 2.5;\n"                             # 3
    "    array a = [1, 2, 3];\n"                       # 4
    "    json j = {\"k\": 7, \"s\": \"x\"};\n"            # 5
    "    bool b = true;\n"                             # 6
    "    float g = 1.0;\n"                             # 7
    "    float[] fa = array_fill(2, 0.25);\n"          # 8
    "    var yok = null;\n"                            # 9
    "    array sp = split(\"x,yy\", \",\");\n"          # 10 kutusuz dizgi deposu
    "    P[] pa = [];\n"                               # 11 tipli struct dizisi
    "    push(pa, Nk);\n"                              # 12
    "    T[] ta = [];\n"                               # 13 f32/i32 alanli (C yerlesimi)
    "    push(ta, Tk);\n"                              # 14
    "    array bx = [Nk];\n"                           # 15 kutulu struct (ad etiketi)
    "    S s = { ad: \"z\", n: 3 };\n"                  # 16 str alanli struct (kutulu)
    "    print(ad);\n"                                 # 17 <- breakpoint
    "}\n"                                              # 18
    "struct P { int x; float y; bool ok; }\n"          # 19
    "struct T { f32 a; i32 b; bool c; }\n"             # 20
    "struct S { str ad; int n; }\n"                    # 21
    "P Nk = { x: 1, y: 2.5, ok: true };\n"            # 22
    "T Tk = { a: 0.5, b: 7, c: false };\n"            # 23
    "goster(4);\n"                                     # 24
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


def scenario_values(exe, work):
    """Yerel değerler OKUNUR mu (K153)? Her Tulpar yereli DWARF'ta 128 bitlik
    opak `VMValue`; pretty-printer olmadan `ad` için 130514698818214998946349060
    gibi bir sayı geliyordu. Bağdaştırıcı gömülü tools/gdb/tulpar_printers.py'yi
    yükleyince dizgi/float/dizi/json/bool çözülmeli.

    İkinci ayak: `tulpar debug --gdb-script` çıktısı düz gdb'de de aynı işi
    yapmalı (kurulu tulpar'da tools/ dizini yok)."""
    a = start(exe, work, PROG_VALS, [{"line": 17}])
    try:
        a.wait(is_event("stopped"), "breakpoint'te `stopped`")
        _, vars_ = locals_of(a)
        # g / fa / yok: değer print(x) ile AYNI metinle (Tuzaklar 7j,
        # 2026-10-02). Eskiden printer `g = 1.0` (program `1`), `yok = void`
        # (program `null`) ve kutusuz float[] için double'ın bit desenini int
        # diye gösteriyordu.
        want = {"n": "4", "ad": '"Hamza"', "f": "2.5", "a": "[1, 2, 3]",
                "j": '{"k": 7, "s": "x"}', "b": "true", "g": "1",
                "fa": "[0.25, 0.25]", "yok": "null",
                # split sonucu kutusuz dizgi deposunda (ObjString* tablosu,
                # 2026-10-06): printer onu tamsayi diye okumamali.
                "sp": '["x", "yy"]',
                # Struct dizisi ve kutulu struct print ile AYNI metin
                # (2026-10-06): eskiden `<struct_array @0x...>` ve ad
                # etiketsiz `[{"x": 1, ...}]`.
                "pa": "[P { x: 1, y: 2.5, ok: true }]",
                "ta": "[T { a: 0.5, b: 7, c: false }]",
                "bx": "[P { x: 1, y: 2.5, ok: true }]",
                "s": 'S { ad: "z", n: 3 }'}
        bad = {k: vars_.get(k) for k, v in want.items() if vars_.get(k) != v}
        if bad:
            raise AssertionError("okunmayan degerler: %s (hepsi: %s)" % (bad, vars_))
        a.call("continue", {"threadId": 1})
        run_to_exit(a)
    finally:
        a.close()
    # --gdb-script + düz gdb
    script = os.path.join(work, "t.py")
    with open(script, "w") as fh:
        fh.write(subprocess.run([exe, "debug", "--gdb-script"], capture_output=True,
                                text=True).stdout)
    binp = os.path.join(work, "prog_bin")
    subprocess.run([exe, "--debug", "build", os.path.join(work, "prog.tpr"), binp],
                   capture_output=True, cwd=work)
    r = subprocess.run(["gdb", "-batch", "-nx", "-ex", "source " + script,
                        "-ex", "break prog.tpr:17", "-ex", "run", "-ex", "info locals",
                        binp], capture_output=True, text=True, cwd=work, timeout=TIMEOUT)
    if ('ad = "Hamza"' not in r.stdout or "a = [1, 2, 3]" not in r.stdout or
            "bx = [P { x: 1, y: 2.5, ok: true }]" not in r.stdout or
            "pa = [P { x: 1, y: 2.5, ok: true }]" not in r.stdout):
        raise AssertionError("--gdb-script ile duz gdb degerleri cozmedi:\n%s"
                             % r.stdout[-400:])
    return ("okunur degerler: ad=\"Hamza\" f=2.5 a=[1, 2, 3] j={...} b=true "
            "pa/ta=[P {...}] bx=[P {...}] s=S {...} (DAP + --gdb-script ile duz gdb)")


def scenario_async_threads(exe, work):
    """Async coroutine'leri DAP'ta ayri "thread" olarak gorunuyor mu (K156)?

    hesapla:11'de durulur. O anda iki seviye_yukle (1, 2) ilk await'lerinde
    kendi veri_oku gorevlerini BEKLIYOR, iki veri_oku henuz baslamadi, hesapla
    kosuyor. `threads` main + bes coroutine dondurmeli; bekleyen bir
    seviye_yukle'nin stackTrace'i await zinciri: seviye_yukle -> veri_oku.

    POZITIF KONTROL: ayni oturumda async OLMAYAN program (PROG_FUNC) tek
    thread (main) dondurmeli — liste programdan geliyor, sabit degil."""
    a = start(exe, work, PROG_ASYNC, [{"line": 11}])
    try:
        a.wait(is_event("stopped"), "hesapla:11'de `stopped`")
        th = a.call("threads")["body"]["threads"]
        names = {t["id"]: t["name"] for t in th}
        co = {i: n for i, n in names.items() if i != 1}
        if 1 not in names or len(co) != 5:
            raise AssertionError("threads: main + 5 coroutine bekleniyordu: %s" % names)
        running = [n for n in co.values() if "hesapla" in n]
        waiting = [i for i, n in co.items() if "seviye_yukle" in n and "veri_oku" in n]
        if len(running) != 1 or len(waiting) != 2:
            raise AssertionError("coroutine adlari/durumlari beklenmedik: %s" % co)
        st = a.call("stackTrace", {"threadId": waiting[0]})["body"]["stackFrames"]
        chain = [f["name"].split(" ")[0] for f in st]
        if chain != ["seviye_yukle", "veri_oku"]:
            raise AssertionError("await zinciri %s (beklenen seviye_yukle -> veri_oku)" % chain)
        sc = a.call("scopes", {"frameId": st[0]["id"]})["body"]["scopes"]
        if sc:
            raise AssertionError("sentetik cercevenin scopes'u bos olmali: %s" % sc)
        top = a.call("stackTrace", {"threadId": 1})["body"]["stackFrames"][0]
        if top.get("name") not in ("hesapla", "t_hesapla") or top.get("line") != 11:
            raise AssertionError("thread 1 ust cerceve %s:%s" % (top.get("name"), top.get("line")))
        a.call("continue", {"threadId": 1})
        run_to_exit(a)
    finally:
        a.close()
    a = start(exe, work, PROG_FUNC, [{"line": 3}])
    try:
        a.wait(is_event("stopped"), "kontrol: topla:3'te `stopped`")
        th = a.call("threads")["body"]["threads"]
        if [t["id"] for t in th] != [1]:
            raise AssertionError("async'siz programda threads: %s (beklenen yalniz main)" % th)
        a.call("continue", {"threadId": 1})
        run_to_exit(a)
    finally:
        a.close()
    return ("async: main + 5 coroutine thread, bekleyenin await zinciri "
            "seviye_yukle -> veri_oku; async'siz programda yalniz main (kontrol)")


def scenario_data(exe, work):
    """Veri breakpoint'i: `t` değiştikçe durmalı, değerler programın kendi
    hesabı (0+0, +10, +20, +30, +40). Pozitif kontrol: i=0 turu `t`'yi
    DEĞİŞTİRMEZ (0 -> 0), yani dört durma "her yazmada" değil "her değişimde"
    — bir izleyicinin gerçekten değeri izlediğinin kanıtı."""
    a = start(exe, work, PROG_LOOP, [{"line": 5}])
    try:
        a.wait(is_event("stopped"), "satır 5'te `stopped`")
        info = a.call("dataBreakpointInfo", {"name": "t"})["body"]
        if not info.get("dataId"):
            raise AssertionError("dataBreakpointInfo dataId vermedi: %s" % info)
        r = a.call("setDataBreakpoints", {"breakpoints": [
            {"dataId": info["dataId"], "accessType": "write"}]})
        got = r["body"]["breakpoints"]
        if len(got) != 1 or not got[0].get("verified"):
            raise AssertionError("setDataBreakpoints doğrulanmadı: %s" % got)
        a.call("setBreakpoints", {"source": {"path": os.path.join(work, "prog.tpr")},
                                  "breakpoints": []})
        a.call("continue", {"threadId": 1})
        vals, reasons = [], set()
        while True:
            m = a.wait(lambda m: m.get("type") == "event" and
                       m.get("event") in ("stopped", "terminated"), "stopped/terminated")
            if m["event"] == "terminated":
                break
            reasons.add(m.get("body", {}).get("reason"))
            fr = a.call("stackTrace", {"threadId": 1})["body"]["stackFrames"][0]
            vals.append(a.call("evaluate", {"expression": "t",
                                            "frameId": fr["id"]})["body"]["result"])
            if len(vals) > 8:
                raise AssertionError("izleyici durmuyor: %s" % vals)
            a.call("continue", {"threadId": 1})
        if vals != ["10", "30", "60", "100"]:
            raise AssertionError("veri breakpoint degerleri %s (beklenen 10,30,60,100)" % vals)
        if reasons != {"data breakpoint"}:
            raise AssertionError("stopped.reason %s (beklenen 'data breakpoint')" % reasons)
        return "veri breakpoint'i: t yazma izleyicisi 4 durma, t=10,30,60,100, reason='data breakpoint'"
    finally:
        a.close()


def scenario_instruction(exe, work):
    """Komut breakpoint'i: adres, ilk durmanın stackTrace'inden
    (`instructionPointerReference`) — istemcinin elindeki tek adres kaynağı.
    Pozitif kontrol: kaynak breakpoint'i silindikten sonra gelen her durma
    komut breakpoint'inin eseri; `i` 1..4 onların DÖRT ayrı çağrı olduğunu
    gösteriyor."""
    a = start(exe, work, PROG_LOOP, [{"line": 2}])
    try:
        a.wait(is_event("stopped"), "satır 2'de `stopped`")
        frames, vars_ = locals_of(a)
        ip = frames[0].get("instructionPointerReference")
        if not ip or vars_.get("i") != "0":
            raise AssertionError("ilk durma: ip=%r i=%r" % (ip, vars_.get("i")))
        a.call("setBreakpoints", {"source": {"path": os.path.join(work, "prog.tpr")},
                                  "breakpoints": []})
        r = a.call("setInstructionBreakpoints", {"breakpoints": [
            {"instructionReference": ip}]})
        got = r["body"]["breakpoints"]
        if len(got) != 1 or not got[0].get("verified"):
            raise AssertionError("setInstructionBreakpoints doğrulanmadı: %s" % got)
        a.call("continue", {"threadId": 1})
        seen = []
        while True:
            m = a.wait(lambda m: m.get("type") == "event" and
                       m.get("event") in ("stopped", "terminated"), "stopped/terminated")
            if m["event"] == "terminated":
                break
            fr, vs = locals_of(a)
            if fr[0].get("instructionPointerReference") != ip:
                raise AssertionError("başka adreste durdu: %s (beklenen %s)" %
                                     (fr[0].get("instructionPointerReference"), ip))
            seen.append(vs.get("i"))
            if len(seen) > 8:
                raise AssertionError("komut breakpoint'i durmuyor: %s" % seen)
            a.call("continue", {"threadId": 1})
        if seen != ["1", "2", "3", "4"]:
            raise AssertionError("komut breakpoint'inde i=%s (beklenen 1..4)" % seen)
        return "komut breakpoint'i: %s adresinde 4 durma, i=1..4" % ip
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
    for fn in (scenario_breakpoint, scenario_conditional, scenario_logpoint,
               scenario_values, scenario_async_threads, scenario_data,
               scenario_instruction):
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
    print("dap denetimi temiz (breakpoint + kosullu + logpoint + degerler + async coroutine'ler + "
          "veri + komut breakpoint'i; stopped/exited/terminated olaylari)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
