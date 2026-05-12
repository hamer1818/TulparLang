#include "builtins.hpp"

#include <cstring>

namespace tulpar {

namespace {

// Curated list of the builtins users hit most often. Not exhaustive — the
// hover/completion experience degrades gracefully for missing entries
// (the editor just doesn't show docs). New entries land here when they
// graduate from "ad-hoc codegen helper" to "official surface area".
//
// Ordering doesn't matter; lookup is linear (size << 1k).
const BuiltinEntry kBuiltins[] = {
    // ---- I/O ----
    {"print",        "print(value: any): void",                     "Stdout'a yazar (her tip; satır sonu eklenir)."},
    {"input",        "input(): str",                                "Stdin'den bir satır okur."},
    {"input_int",    "input_int(): int",                            "Stdin'den sayı okur."},
    {"input_float",  "input_float(): float",                        "Stdin'den ondalıklı sayı okur."},

    // ---- String / collection ----
    {"len",          "len(value: any): int",                        "Dizi/JSON/string uzunluğu."},
    {"length",       "length(value: any): int",                     "len() ile aynı; eski API."},
    {"push",         "push(arr: array, value: any): void",          "Dizinin sonuna ekler."},
    {"pop",          "pop(arr: array): any",                        "Dizinin son öğesini çıkarır."},
    {"trim",         "trim(s: str): str",                           "Baş/son boşlukları siler."},
    {"upper",        "upper(s: str): str",                          "Büyük harfe çevirir."},
    {"lower",        "lower(s: str): str",                          "Küçük harfe çevirir."},
    {"capitalize",   "capitalize(s: str): str",                     "Baş harfi büyük yapar."},
    {"reverse",      "reverse(s: str): str",                        "String'i tersine çevirir."},
    {"isEmpty",      "isEmpty(s: str): bool",                       "Boş string kontrolü."},
    {"isDigit",      "isDigit(s: str): bool",                       "Tüm karakterler rakam mı?"},
    {"isAlpha",      "isAlpha(s: str): bool",                       "Tüm karakterler harf mi?"},
    {"replace",      "replace(s: str, old: str, new: str): str",    "Tüm `old` geçişlerini `new` ile değiştirir."},
    {"split",        "split(s: str, sep: str): array<str>",         "Ayraç üzerinden böler."},
    {"substring",    "substring(s: str, start: int, end: int): str", "[start, end) aralığını döner."},
    {"contains",     "contains(s: str, needle: str): bool",         "Alt string araması."},
    {"range",        "range(n: int): array<int>",                   "[0, n) aralığında dizi üretir."},

    // ---- Conversion ----
    {"toString",     "toString(value: any): str",                   "Herhangi bir değeri string'e çevirir."},
    {"toInt",        "toInt(value: any): int",                      "int parse / cast."},
    {"toFloat",      "toFloat(value: any): float",                  "float parse / cast."},
    {"toJson",       "toJson(value: any): str",                     "Değeri JSON string'ine serileştirir."},
    {"fromJson",     "fromJson(s: str): json",                      "JSON string'i tipsiz değere parse eder."},

    // ---- Math ----
    {"abs",          "abs(x: int|float): int|float",                "Mutlak değer."},
    {"sqrt",         "sqrt(x: float): float",                       "Karekök."},
    {"pow",          "pow(base: float, exp: float): float",         "Üs alma."},
    {"floor",        "floor(x: float): float",                      "Aşağı yuvarlama."},
    {"ceil",         "ceil(x: float): float",                       "Yukarı yuvarlama."},
    {"round",        "round(x: float): float",                      "En yakına yuvarlama."},
    {"min",          "min(a, b): float",                            "İki değerin küçüğü."},
    {"max",          "max(a, b): float",                            "İki değerin büyüğü."},
    {"mod",          "mod(a: int, b: int): int",                    "Tamsayı modulo (a % b)."},
    {"fmod",         "fmod(a: float, b: float): float",             "Float modulo."},
    {"random",       "random(): float",                             "[0.0, 1.0) aralığında rastgele float."},
    {"randint",      "randint(min: int, max: int): int",            "[min, max] aralığında rastgele int."},
    {"sin",          "sin(x: float): float",                        ""},
    {"cos",          "cos(x: float): float",                        ""},
    {"tan",          "tan(x: float): float",                        ""},
    {"log",          "log(x: float): float",                        "Doğal log (ln)."},
    {"log10",        "log10(x: float): float",                      ""},
    {"log2",         "log2(x: float): float",                       ""},
    {"exp",          "exp(x: float): float",                        "e^x."},

    // ---- Time ----
    {"clock_ms",     "clock_ms(): float",                           "Yüksek hassasiyetli zamanlayıcı (ms)."},
    {"timestamp",    "timestamp(): int",                            "Unix epoch (saniye)."},
    {"time_ms",      "time_ms(): int",                              "Unix epoch (milisaniye)."},
    {"sleep",        "sleep(ms: int): void",                        "Verilen milisaniye kadar bekler."},

    // ---- File ----
    {"read_file",    "read_file(path: str): str",                   "Dosyayı tamamen okur."},
    {"write_file",   "write_file(path: str, data: str): bool",      "Dosyayı yeniden yazar."},
    {"append_file",  "append_file(path: str, data: str): bool",     "Dosyaya ekler."},
    {"file_exists",  "file_exists(path: str): bool",                "Dosya/dizin var mı?"},

    // ---- DB ----
    {"db_open",      "db_open(path: str): int",                     "SQLite veritabanı açar."},
    {"db_close",     "db_close(handle: int): void",                 ""},
    {"db_execute",   "db_execute(handle: int, sql: str): int",      "INSERT/UPDATE/DELETE yürütür."},
    {"db_query",     "db_query(handle: int, sql: str): array<json>", "SELECT döner."},
    {"db_last_insert_id", "db_last_insert_id(handle: int): int",    ""},
    {"db_error",     "db_error(handle: int): str",                  "Son hatayı döner."},

    // ---- HTTP ----
    {"http_parse_request",  "http_parse_request(raw: str): json",   "Ham HTTP isteğini parçalar."},
    {"http_create_response","http_create_response(status: int, ct: str, body: str): str", "HTTP yanıtı oluşturur."},
    {"http_status_text",    "http_status_text(code: int): str",     "200→\"OK\", 404→\"Not Found\", …"},
    {"path_match",          "path_match(pattern: str, path: str): json", "/users/:id ile gelen path'i eşler."},
    {"parse_query",         "parse_query(qs: str): json",           "?a=1&b=2 → {a: \"1\", b: \"2\"}"},
    {"parse_cookies",       "parse_cookies(header: str): json",     "Cookie header'ı parse eder: \"a=1; b=2\" → {a: \"1\", b: \"2\"}"},

    // ---- Crypto / encoding ----
    {"sha1",                "sha1(s: str): str",                    "20-baytlık ikili SHA-1 özeti döner."},
    {"sha1_hex",            "sha1_hex(s: str): str",                "40 karakter küçük-harf hex SHA-1."},
    {"base64_encode",       "base64_encode(s: str): str",           "Bayt dizisini base64'e çevirir (padding `=` ile)."},
    {"base64_decode",       "base64_decode(s: str): str",           "Base64'ten bayt dizisine. Hatalı girdi → boş str."},
    {"wings_ws_accept_key", "wings_ws_accept_key(client_key: str): str", "RFC 6455 §4.2.2 handshake: base64(sha1(key + GUID))."},
    {"wings_ws_send_frame", "wings_ws_send_frame(fd: int, opcode: int, payload: str): int", "WebSocket frame yazar (FIN=1, unmasked); 1=text, 2=binary, 8=close, 9=ping, 10=pong."},
    {"wings_ws_recv_frame", "wings_ws_recv_frame(fd: int): json",    "WebSocket frame okur, masking key uygular. {ok, opcode, fin, payload} ya da {ok=0, error}."},

    // ---- Socket ----
    {"socket_server",  "socket_server(host: str, port: int): int",  ""},
    {"socket_client",  "socket_client(host: str, port: int): int",  ""},
    {"socket_accept",  "socket_accept(server_fd: int): int",        ""},
    {"socket_send",    "socket_send(client_fd: int, data: str): int", ""},
    {"socket_receive", "socket_receive(client_fd: int, size: int): str", ""},
    {"socket_close",   "socket_close(fd: int): void",               ""},

    // ---- Thread ----
    {"thread_create",  "thread_create(func_name: str, arg: any): int", ""},
    {"thread_join",    "thread_join(thread_id: int): void",         ""},
    {"thread_detach",  "thread_detach(thread_id: int): void",       ""},
    {"mutex_create",   "mutex_create(): int",                       ""},
    {"mutex_lock",     "mutex_lock(mtx: int): void",                ""},
    {"mutex_unlock",   "mutex_unlock(mtx: int): void",              ""},
    {"mutex_destroy",  "mutex_destroy(mtx: int): void",             ""},

    // ---- Misc ----
    {"call",         "call(name: str, ...): any",                   "İsme göre fonksiyon çağırır (handler dispatch)."},
    {"exit",         "exit(code: int): void",                       "Süreci verilen kodla sonlandırır."},
    {"sb_append",    "sb_append(sb: int, s: str): void",            "StringBuilder'a ekler."},
    {"sb_tostring",  "sb_tostring(sb: int): str",                   "StringBuilder içeriğini döner."},
    {"sb_free",      "sb_free(sb: int): void",                      ""},
};

const size_t kBuiltinCount = sizeof(kBuiltins) / sizeof(kBuiltins[0]);

}  // namespace

const BuiltinEntry *builtin_table(size_t *out_count) {
    if (out_count) *out_count = kBuiltinCount;
    return kBuiltins;
}

const BuiltinEntry *builtin_lookup(const char *name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < kBuiltinCount; i++) {
        if (std::strcmp(kBuiltins[i].name, name) == 0) return &kBuiltins[i];
    }
    return nullptr;
}

}  // namespace tulpar
