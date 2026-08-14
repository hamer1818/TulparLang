#!/usr/bin/env python3
"""Builtin tablosu ↔ codegen ↔ LSP tutarlılık denetimi.

Bir builtin üç yerde birden bilinmek zorunda:

  1. codegen   (src/aot/llvm_backend.cpp) — çağrı gerçekten emit edilir
  2. typeinfer (src/typeinfer/typeinfer.cpp) — argüman/dönüş denetlenir
  3. LSP       (src/lsp/builtins.cpp) — tamamlama/hover

Bunlar elle senkron tutuluyordu ve ayrışmışlardı. Ayrışmanın iki yönü de
sessiz, ikisi de ölçüldü:

  * **Tabloda var, codegen'de yok** → typecheck "sorun yok" der, kullanıcı
    çalışma anında "fonksiyon bulunamadı" alır. (`clock`, `toUpper`, ...)
  * **Codegen'de var, tabloda yok** → o çağrı HİÇ denetlenmez. Ölçüldü:
    `str s = len("abc")` yakalanıyor ama `str s = pow("a","b")` sessizce
    geçiyordu, çünkü tabloda olmayan builtin'in dönüşü VOID sayılıyor ve
    sonraki denetimler de atlanıyor.

`assert`'in yıllarca sessiz no-op kalması tam bu aileden bir hataydı: bir
liste gerçekliği yansıtmıyordu ve bunu söyleyen hiçbir şey yoktu. Bu betik
o söyleyen şeydir.

Kullanım:  python3 tests/builtin_audit.py [--list]
Çıkış kodu: 0 temiz, 1 YENİ ayrışma var.
"""

import re
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Bilerek tabloda olmayanlar. Bunlar kullanıcı yüzü değil: derleyicinin/
# stdlib'in iç plumbing'i, wings/tls/thread altyapısı, arena yönetimi.
# Bir builtin'i buraya eklemek "denetlenmesine gerek yok" demektir — gerekçesi
# olmadan ekleme.
INTERNAL = {
    # derleyici/çalışma zamanı iç işleri
    "main", "_request", "persist", "string_pin", "typeof",
    "arena_save", "arena_restore", "arena_drop",
    # StringBuilder (lib tarafından sarmalanıyor)
    "StringBuilder", "sb_append", "sb_free", "sb_tostring",
    # wings/http/tls/websocket altyapısı — lib/*.tpr sarmalıyor
    "http_create_response", "http_parse_request", "http_recv_request",
    "http_should_keepalive", "http_status_text",
    "wings_build_response", "wings_current_fd", "wings_find_route",
    "wings_set_current_fd", "wings_ws_accept_key", "wings_ws_recv_frame",
    "wings_ws_send_frame",
    "tls_accept", "tls_close", "tls_ctx_free", "tls_init", "tls_recv",
    "tls_send",
    # iş parçacığı / mutex / socket alt seviye
    "mutex_create", "mutex_destroy", "mutex_lock", "mutex_unlock",
    "thread_detach", "thread_join", "socket_poll",
    "socket_set_nonblocking", "cpu_count", "gather",
    # db iç
    "db_error", "db_last_insert_id",
}

# HENÜZ kapatılmamış boşluklar. Bunlar kullanıcı yüzü VE denetlenmiyor —
# yani gerçek borç. Listeden silmek = tabloya imza eklemek.
# TODO.md §3'te takip ediliyor.
KNOWN_GAPS = {
    # tabloda var, codegen'de YOK (kırık vaat)
    "clock", "toBool", "toLower", "toUpper", "values",
    "socket_recv", "socket_select",
    # codegen'de var, tabloda YOK (denetimsiz)
    "print", "random", "randint", "min", "max", "mod",
    "join", "reverse", "repeat", "count", "capitalize",
    "isAlpha", "isDigit", "isEmpty", "isArray", "isBool", "isFloat",
    "isInt", "isObject", "isString",
    "base64_decode", "base64_encode", "sha1", "sha1_hex",
    "csv_emit", "csv_parse", "file_glob", "path_match", "parse_multipart",
    "http_request", "http_request_async",
    "input_float", "input_int",
    "time_ms", "timestamp", "weekday", "now_iso8601", "format_iso8601",
    "parse_iso8601", "date_add_seconds",
}


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as f:
        return f.read()


def collect():
    cg = read("src/aot/llvm_backend.cpp")
    ti = read("src/typeinfer/typeinfer.cpp")
    ls = read("src/lsp/builtins.cpp")

    codegen = set(re.findall(
        r'strcmp\(\s*node->name\s*,\s*"([A-Za-z_][A-Za-z_0-9]*)"\s*\)', cg))
    # makro tabanlı dağıtım: MATH1_FUNC("sin", ...) vb.
    codegen |= set(re.findall(
        r'\b[A-Z][A-Z0-9_]*_FUNC\(\s*"([A-Za-z_][A-Za-z_0-9]*)"', cg))
    # tame tablosu: {"tm_x", "aot_tm_x_ptr", n}
    codegen |= set(re.findall(r'\{"(tm3?_[A-Za-z_0-9]+)",\s*"aot_', cg))
    codegen |= set(re.findall(
        r'node->name\s*==\s*"([A-Za-z_][A-Za-z_0-9]*)"', cg))

    typeinf = set(re.findall(
        r'\{"([A-Za-z_][A-Za-z_0-9]*)",\s*TYPE_[A-Z_]+,\s*\{', ti))
    lsp = set(re.findall(r'\{"([A-Za-z_][A-Za-z_0-9]*)",\s*"', ls))
    return codegen, typeinf, lsp


def main():
    codegen, typeinf, lsp = collect()
    excused = INTERNAL | KNOWN_GAPS

    phantom = sorted((typeinf - codegen) - excused)   # tablo yalan söylüyor
    unchecked = sorted((codegen - typeinf) - excused)  # denetimsiz
    no_lsp = sorted(((codegen & typeinf) - lsp) - excused)

    if "--list" in sys.argv:
        print("codegen=%d typeinfer=%d lsp=%d" %
              (len(codegen), len(typeinf), len(lsp)))
        print("bilinen boşluk=%d, iç=%d" % (len(KNOWN_GAPS), len(INTERNAL)))

    bad = False
    if phantom:
        bad = True
        print("HATA: typeinfer tablosunda VAR ama codegen'de YOK "
              "(çağrılınca 'fonksiyon bulunamadı'):")
        for n in phantom:
            print("   -", n)
    if unchecked:
        bad = True
        print("HATA: codegen'de VAR ama typeinfer tablosunda YOK "
              "(bu çağrılar HİÇ denetlenmiyor):")
        for n in unchecked:
            print("   -", n)
    if no_lsp:
        bad = True
        print("HATA: her iki tarafta var ama LSP'de yok "
              "(tamamlama/hover kaybı):")
        for n in no_lsp:
            print("   -", n)

    if bad:
        print("\nYeni builtin eklerken 5 noktanın hepsine dokunun "
              "(bkz. CLAUDE.md '5 noktalı bağlama').")
        print("Bilerek denetim dışı bırakıyorsanız tests/builtin_audit.py "
              "içindeki INTERNAL listesine GEREKÇESİYLE ekleyin.")
        return 1

    print("builtin denetimi temiz (%d bilinen boşluk takip ediliyor)"
          % len(KNOWN_GAPS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
