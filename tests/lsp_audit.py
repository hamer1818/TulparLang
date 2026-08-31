#!/usr/bin/env python3
"""`tulpar --lsp` duman testi — editör eklentisinin dayandığı yüzey.

NEDEN: LSP hiçbir otomasyonda yoktu (eski `lsp_smoke.py` kaldırılmıştı ve
zaten CI'a bağlı değildi). Bozulsa kimse fark etmezdi: derleyici yeşil kalır,
süitler yeşil kalır, yalnız editörde tamamlama/hover sessizce ölür. Bugün
denetimsiz kalan üç yüzeyden ikisi (web arşivleri, `tulpar fmt`) gerçekten
kırık çıktı — bu, üçüncüsünün kırılmadan önce yakalanması için.

EN GÜÇLÜ DENETİM "İLAN ET ↔ UYGULA": `initialize` hangi `*Provider`
yeteneğini ilan ediyorsa, o metot GERÇEKTEN çağrılıp anlamlı cevap verdiği
sınanıyor. İlan edip uygulamamak, editörde "hiçbir şey olmuyor" demek ve
hiçbir log satırı üretmiyor.
"""
import json
import os
import queue
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
URI = "file:///tmp/tulpar_lsp_audit.tpr"
GOOD = ("func topla(int a, int b): int {\n"
        "    return a + b;\n"
        "}\n"
        "int s = topla(1, 2);\n"
        "print(s);\n")
BAD = "int x = ;\nprint(x)\n"


def frame(obj):
    body = json.dumps(obj).encode("utf-8")
    return b"Content-Length: %d\r\n\r\n%s" % (len(body), body)


class Server:
    def __init__(self, exe):
        self.proc = subprocess.Popen([exe, "--lsp"], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, cwd=ROOT)
        self.msgs = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        out = self.proc.stdout
        while True:
            line = out.readline()
            if not line:
                return
            if line.lower().startswith(b"content-length:"):
                n = int(line.split(b":")[1].strip())
                out.readline()
                try:
                    self.msgs.put(json.loads(out.read(n)))
                except Exception:
                    pass

    def send(self, obj):
        self.proc.stdin.write(frame(obj))
        self.proc.stdin.flush()

    def wait(self, pred, timeout=10.0):
        """Eşleşmeyen mesajlar KUYRUKTA kalıyor: tanı bildirimleri istek
        cevaplarının arasına karışıyor ve atılsalardı sonraki bekleyiş
        onları kaçırırdı."""
        end = time.time() + timeout
        held = []
        found = None
        while time.time() < end:
            try:
                msg = self.msgs.get(timeout=0.2)
            except queue.Empty:
                continue
            if pred(msg):
                found = msg
                break
            held.append(msg)
        for h in held:
            self.msgs.put(h)
        return found

    def request(self, rid, method, params):
        self.send({"jsonrpc": "2.0", "id": rid, "method": method,
                   "params": params})
        return self.wait(lambda m: m.get("id") == rid)


FAILS = []


def check(cond, label, detail=""):
    if cond:
        return True
    FAILS.append(label + (": " + str(detail) if detail else ""))
    return False


def main():
    exe = os.path.join(ROOT, "tulpar")
    if not os.access(exe, os.X_OK):
        print("lsp denetimi: ./tulpar yok — atlandi")
        return 0
    srv = Server(exe)
    try:
        init = srv.request(1, "initialize",
                           {"processId": None, "rootUri": None,
                            "capabilities": {}})
        if not check(init is not None, "initialize cevap vermedi"):
            return 1
        caps = init.get("result", {}).get("capabilities", {})
        check(bool(caps), "initialize yetenek bildirmedi")
        srv.send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
        srv.send({"jsonrpc": "2.0", "method": "textDocument/didOpen",
                  "params": {"textDocument": {"uri": URI, "languageId": "tulpar",
                                              "version": 1, "text": GOOD}}})
        d = srv.wait(lambda m: m.get("method") == "textDocument/publishDiagnostics")
        check(d is not None, "acilista tani bildirimi gelmedi")
        if d is not None:
            check(d["params"]["diagnostics"] == [], "saglam kodda tani var",
                  d["params"]["diagnostics"])

        # KONUMLAR METİNDEN hesaplanıyor, elle yazılmıyor: ilk yazımda sabit
        # sütun numaraları kullandım, sonra örnek metni kısalttım ve aynı
        # sütun parantezin İÇİNE düştü — denetim sunucuyu değil kendi
        # aritmetiğini kızarttı. Konumu metinden türetmek bunu imkânsız kılar.
        call_line = GOOD.split("\n")[3]              # "int s = topla(1, 2);"
        c_ident = call_line.index("topla") + 2        # tanımlayıcının İÇİ
        c_arg1 = call_line.index("(") + 1             # 1. argüman
        c_arg2 = call_line.index(",") + 2             # 2. argüman
        pos = {"textDocument": {"uri": URI},
               "position": {"line": 3, "character": c_ident}}

        # İLAN ET ↔ UYGULA: her Provider yeteneği gerçekten çağrılıyor.
        if caps.get("hoverProvider"):
            r = srv.request(2, "textDocument/hover", pos)
            val = ((r or {}).get("result") or {}).get("contents", {})
            text = val.get("value", "") if isinstance(val, dict) else str(val)
            check("topla" in text, "hover imzayi vermiyor", text[:80])
        if caps.get("definitionProvider"):
            r = srv.request(3, "textDocument/definition", pos)
            res = (r or {}).get("result")
            check(bool(res), "definition bos")
            if res:
                loc = res[0] if isinstance(res, list) else res
                check(loc.get("range", {}).get("start", {}).get("line") == 0,
                      "definition bildirim satirini gostermiyor", loc)
        if caps.get("referencesProvider"):
            r = srv.request(4, "textDocument/references",
                            {**pos, "context": {"includeDeclaration": True}})
            res = (r or {}).get("result") or []
            check(len(res) >= 2, "references bildirim+kullanimi vermiyor", len(res))
        if caps.get("completionProvider"):
            r = srv.request(5, "textDocument/completion",
                            {"textDocument": {"uri": URI},
                             "position": {"line": 4, "character": 3}})
            res = (r or {}).get("result")
            items = res.get("items") if isinstance(res, dict) else res
            check(items, "completion bos")
        if caps.get("signatureHelpProvider"):
            # İMLEÇ PARANTEZ İÇİNDE olmalı; tanımlayıcının üstünde null dönmesi
            # doğru davranış. Aktif parametrenin İLERLEMESİ de sınanıyor —
            # imza gösterip parametreyi hep 0 vermek sessiz bir gerilemedir.
            a = srv.request(6, "textDocument/signatureHelp",
                            {"textDocument": {"uri": URI},
                             "position": {"line": 3, "character": c_arg1}})
            b = srv.request(7, "textDocument/signatureHelp",
                            {"textDocument": {"uri": URI},
                             "position": {"line": 3, "character": c_arg2}})
            ra = (a or {}).get("result") or {}
            rb = (b or {}).get("result") or {}
            check(ra.get("signatures"), "signatureHelp imza vermiyor")
            check(ra.get("activeParameter") == 0 and rb.get("activeParameter") == 1,
                  "signatureHelp aktif parametreyi izlemiyor",
                  (ra.get("activeParameter"), rb.get("activeParameter")))

        if caps.get("renameProvider"):
            r = srv.request(8, "textDocument/rename",
                            {**pos, "newName": "toplam"})
            edit = (r or {}).get("result") or {}
            changes = edit.get("changes") or {}
            edits = changes.get(URI) or []
            # Bildirim + kullanım: ikisi birden değişmezse yeniden adlandırma
            # kodu BOZAR (bir tarafı eski adda kalır) — sessiz ve kötü.
            check(len(edits) >= 2, "rename bildirim+kullanimi kapsamiyor",
                  len(edits))
            check(all(e.get("newText") == "toplam" for e in edits),
                  "rename yeni adi yazmiyor", edits[:2])

        # BOZUK kod tanı üretmeli — editörün kırmızı altçizgisi buradan geliyor.
        srv.send({"jsonrpc": "2.0", "method": "textDocument/didChange",
                  "params": {"textDocument": {"uri": URI, "version": 2},
                             "contentChanges": [{"text": BAD}]}})
        d2 = srv.wait(lambda m: m.get("method") == "textDocument/publishDiagnostics")
        if check(d2 is not None, "degisiklikte tani bildirimi gelmedi"):
            ds = d2["params"]["diagnostics"]
            check(len(ds) > 0, "BOZUK kodda tani yok (editor sessiz kalir)")
            if ds:
                check(ds[0].get("severity") == 1, "tani siddeti HATA degil",
                      ds[0].get("severity"))

        srv.request(99, "shutdown", {})
        srv.send({"jsonrpc": "2.0", "method": "exit", "params": {}})
        try:
            rc = srv.proc.wait(timeout=5)
            check(rc == 0, "exit sonrasi cikis kodu", rc)
        except subprocess.TimeoutExpired:
            check(False, "exit sonrasi surec KAPANMADI")
    finally:
        if srv.proc.poll() is None:
            srv.proc.kill()

    if FAILS:
        print("lsp denetimi BASARISIZ:")
        for f in FAILS:
            print("   " + f)
        return 1
    print("lsp denetimi temiz (%d yetenek: %s)"
          % (len(caps), ", ".join(sorted(k for k in caps if k.endswith("Provider")))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
