#!/usr/bin/env python3
"""engine/ katman bagimlilik denetimi — ihlal = BUILD HATASI (plan §2).

Kurallar:
  1. Bir katman yalniz ALTINDAKI katmanlari #include eder. Yukari cagri yok.
     platform=L0 core=L1 rhi=L2 renderer=L3 sim=L4 gameplay=L5 content=L6
     app=L6 (birlestirme koku) tools=L7. tests/ her seyi gorebilir.
  2. STL konteyneri / genel ayirici YOK (plan L1 "STL yok", A2): vector, map,
     string, memory (shared_ptr), functional... Testler DAHIL — kapi durust
     kalsin: test kodunda vector kullanmak "0 ayirma" iddiasini gizler.
     Serbest: <atomic> <algorithm> <new> <utility> <cstdio> <cstdint> ...

CMake bunu engine_core'un on kosulu yapar: ihlal varsa kutuphane derlenmez.
"""
import os
import re
import sys

# app = birlestirme koku (executable): content dahil her seyi gorur, tools'u
# gormez. content (L6) icerik boru hattinin runtime yuzu; gameplay icerik
# yuklemeye ihtiyac duyarsa runtime yukleyici asagi (L3.5) ayrilir.
LAYERS = {"platform": 0, "core": 1, "rhi": 2, "renderer": 3, "audio": 3, "sim": 4,
          "gameplay": 5, "content": 6, "app": 6, "bridge": 6, "tools": 7, "tests": 99}
BANNED_STL = {"vector", "map", "unordered_map", "set", "unordered_set",
              "string", "list", "deque", "forward_list", "memory",
              "functional", "sstream", "iostream", "fstream", "queue",
              "stack", "any", "variant", "optional", "regex", "thread",
              "mutex", "shared_mutex", "condition_variable", "future"}
INC_RE = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]')


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    violations = []
    files = 0
    for dirpath, _, names in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        top = rel_dir.split(os.sep)[0] if rel_dir != "." else ""
        if top not in LAYERS or top == "tools":
            continue
        for n in names:
            if not n.endswith((".hpp", ".cpp", ".h", ".c")):
                continue
            path = os.path.join(dirpath, n)
            rel = os.path.relpath(path, root)
            layer = LAYERS[top]
            files += 1
            with open(path, encoding="utf-8", errors="replace") as f:
                for ln, line in enumerate(f, 1):
                    m = INC_RE.match(line)
                    if not m:
                        continue
                    kind, target = m.group(1), m.group(2)
                    if kind == '"':
                        inc_top = target.split("/")[0]
                        inc_layer = LAYERS.get(inc_top)
                        if inc_layer is None:
                            violations.append(f"{rel}:{ln}: bilinmeyen katman dizini '{inc_top}' ({target})")
                        elif layer != 99 and inc_layer > layer:
                            violations.append(f"{rel}:{ln}: L{layer} dosyasi L{inc_layer} basligini iceriyor ({target}) — katman yalniz ALTINI cagirir")
                    else:
                        base = target.split("/")[0]
                        if base in BANNED_STL:
                            violations.append(f"{rel}:{ln}: <{target}> yasak (STL konteyner/ayirici; arena + Array/Span kullan)")
    if violations:
        print("engine katman denetimi: %d IHLAL" % len(violations))
        for v in violations:
            print("  " + v)
        return 1
    print("engine katman denetimi: %d dosya, 0 ihlal" % files)
    return 0


if __name__ == "__main__":
    sys.exit(main())
