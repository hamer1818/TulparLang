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
    {"ord",          "ord(s: str, i: int): int",                    "s'nin i. byte'ının işaretsiz değeri (0-255); i aralık dışıysa -1. String'ler UTF-8 byte dizisi olduğundan (length/substring byte-tabanlı) elle UTF-8 işleme için — örn. tam bir çok-byte kod noktasını silmek (0x80-0xBF devam byte'larını geri sayarak)."},
    {"contains",     "contains(s: str, needle: str): bool",         "Alt string araması."},
    {"range",        "range(n: int): array<int>",                   "[0, n) aralığında dizi üretir."},

    // ---- tame (2D oyun) native katmanı — `import "tame"` sarmalayıcılarının altı.
    // Renk = paketlenmiş int 0xRRGGBBAA (lib/tame.tpr: rgb()/rgba() + adlı renkler).
    {"tm_window",       "tm_window(w: int, h: int, title: str): bool",  "Oyun penceresini açar (varsayılan 60 FPS). Başarısızsa (görüntü ortamı yok) false döner. Sarmalayıcı: window()."},
    {"tm_running",      "tm_running(): bool",                           "Pencere açık ve kapatılmak istenmemişse true — ana oyun döngüsünün koşulu. Sarmalayıcı: running()."},
    {"tm_close",        "tm_close()",                                   "Pencereyi kapatır. Sarmalayıcı: close_window()."},
    {"tm_set_fps",      "tm_set_fps(fps: int)",                         "Hedef kare hızını ayarlar (varsayılan 60). Sarmalayıcı: set_fps()."},
    {"tm_begin",        "tm_begin()",                                   "Kare çizimini başlatır. Sarmalayıcı: frame_begin()."},
    {"tm_end",          "tm_end()",                                     "Kare çizimini bitirir ve ekrana basar (FPS bekleme dahil). Sarmalayıcı: frame_end()."},
    {"tm_fps",          "tm_fps(): int",                                "Anlık FPS. Sarmalayıcı: get_fps()."},
    {"tm_frame_time",   "tm_frame_time(): float",                       "Son karenin süresi (saniye) — kare-bağımsız hareket için çarpan. Sarmalayıcı: frame_time()."},
    {"tm_view_left",    "tm_view_left(): float",                        "Görünür ekranın sol kenarı, dünya koordinatında (Android'de negatif olabilir — bantlar dahil). Sarmalayıcı: view_left()/ekran_sol()."},
    {"tm_view_right",   "tm_view_right(): float",                       "Görünür ekranın sağ kenarı, dünya koordinatında (Android'de dünya genişliğinden büyük olabilir). Sarmalayıcı: view_right()/ekran_sag()."},
    {"tm_view_top",     "tm_view_top(): float",                         "Görünür ekranın üst kenarı, dünya koordinatında. Sarmalayıcı: view_top()/ekran_ust()."},
    {"tm_view_bottom",  "tm_view_bottom(): float",                      "Görünür ekranın alt kenarı, dünya koordinatında. Sarmalayıcı: view_bottom()/ekran_alt()."},
    {"tm_accel_x",      "tm_accel_x(): float",                          "İvmeölçer X ekseni (m/s^2, cihaz doğal yönü: sağ +). Android'de gerçek sensör, masaüstü/web'de 0. Sarmalayıcı: accel_x()/egim_x()."},
    {"tm_accel_y",      "tm_accel_y(): float",                          "İvmeölçer Y ekseni (m/s^2, yukarı +). Sarmalayıcı: accel_y()/egim_y()."},
    {"tm_accel_z",      "tm_accel_z(): float",                          "İvmeölçer Z ekseni (m/s^2, ekrandan dışarı +). Sarmalayıcı: accel_z()/egim_z()."},
    {"tm_accel_available", "tm_accel_available(): bool",                "Cihazda ivmeölçer var ve okunuyor mu. Masaüstü/web: false. Sarmalayıcı: accel_available()/egim_var()."},
    {"tm_active",       "tm_active(): bool",                            "Uygulama önde/odakta mı (arka plana atılınca false). Sarmalayıcı: is_active()/aktif_mi()."},
    {"tm_beep",         "tm_beep(freq: float, ms: int)",                "Dosyasız ses efekti: freq Hz sinüs, ms milisaniye çalar (kısa oyun sesleri). Sarmalayıcı: beep()/bip()."},
    {"tm_tone",         "tm_tone(freq: float, ms: int, vol: float)",    "tm_beep'in ses-seviyeli hali: vol 0..1 genliği ölçekler. Arka plan müziği notalarını SFX'i bastırmadan kısık çalmak için. Sarmalayıcı: tone()/ton()."},
    {"tm_time",         "tm_time(): float",                             "Pencere açıldığından beri geçen süre (saniye). Sarmalayıcı: elapsed()."},
    {"tm_width",        "tm_width(): int",                              "Pencere genişliği (piksel). Sarmalayıcı: screen_width()."},
    {"tm_height",       "tm_height(): int",                             "Pencere yüksekliği (piksel). Sarmalayıcı: screen_height()."},
    {"tm_clear",        "tm_clear(color: int)",                         "Ekranı verilen renge boyar (kare başında çağır). Sarmalayıcı: clear()."},
    {"tm_rect",         "tm_rect(x, y, w, h, color: int)",              "Dolu dikdörtgen çizer. Koordinatlar int/float olabilir. Sarmalayıcı: rect()."},
    {"tm_rect_lines",   "tm_rect_lines(x, y, w, h, color: int)",        "Dikdörtgen çerçevesi çizer. Sarmalayıcı: rect_lines()."},
    {"tm_circle",       "tm_circle(x, y, radius, color: int)",          "Dolu daire çizer. Sarmalayıcı: circle()."},
    {"tm_line",         "tm_line(x1, y1, x2, y2, color: int)",          "Çizgi çizer. Sarmalayıcı: line()."},
    {"tm_pixel",        "tm_pixel(x, y, color: int)",                   "Tek piksel boyar. Sarmalayıcı: pixel()."},
    {"tm_text",         "tm_text(s: str, x, y, size: int, color: int)", "Yazı çizer (varsayılan font). Sarmalayıcı: text()."},
    {"tm_key_down",     "tm_key_down(key: str): bool",                  "Tuş şu an basılı mı? Ad: \"W\", \"SPACE\", \"LEFT\", \"ESC\", \"F1\"... Sarmalayıcı: key_down()."},
    {"tm_key_pressed",  "tm_key_pressed(key: str): bool",               "Tuşa bu karede yeni mi basıldı (tek tetik)? Sarmalayıcı: key_pressed()."},
    {"tm_key_released", "tm_key_released(key: str): bool",              "Tuş bu karede mi bırakıldı? Sarmalayıcı: key_released()."},
    {"tm_mouse_x",      "tm_mouse_x(): int",                            "Fare X konumu (piksel). Sarmalayıcı: mouse_x()."},
    {"tm_mouse_y",      "tm_mouse_y(): int",                            "Fare Y konumu (piksel). Sarmalayıcı: mouse_y()."},
    {"tm_mouse_down",   "tm_mouse_down(button: int): bool",             "Fare düğmesi basılı mı? 0=sol 1=sağ 2=orta. Sarmalayıcı: mouse_down()."},
    {"tm_mouse_pressed","tm_mouse_pressed(button: int): bool",          "Fare düğmesine bu karede mi tıklandı? Sarmalayıcı: mouse_pressed()."},
    {"tm_mouse_wheel",  "tm_mouse_wheel(): float",                      "Tekerlek hareketi (kare başına). Sarmalayıcı: mouse_wheel()."},
    {"tm_touch_count",  "tm_touch_count(): int",                        "Ekrandaki aktif parmak (dokunma noktası) sayısı. Sarmalayıcı: touch_count()/touched()."},
    {"tm_touch_x",      "tm_touch_x(i: int): int",                      "i. parmağın X konumu (piksel). Sarmalayıcı: touch_x()."},
    {"tm_touch_y",      "tm_touch_y(i: int): int",                      "i. parmağın Y konumu (piksel). Sarmalayıcı: touch_y()."},
    {"tm_load_texture", "tm_load_texture(path: str): int",              "PNG/BMP/JPG dosyasını GPU'ya yükler, handle döner (-1 = başarısız). window() sonrası çağrılmalı. Sarmalayıcı: load_texture()."},
    {"tm_draw_texture", "tm_draw_texture(tex: int, x, y)",              "Texture'ı çizer. Sarmalayıcı: draw_texture()."},
    {"tm_draw_texture_ex", "tm_draw_texture_ex(tex: int, x, y, scale, rotation)", "Ölçek + derece döndürmeyle çizer. Sarmalayıcı: draw_texture_ex()."},
    {"tm_texture_width",  "tm_texture_width(tex: int): int",            "Texture genişliği (piksel; geçersiz handle 0). Sarmalayıcı: texture_width()."},
    {"tm_texture_height", "tm_texture_height(tex: int): int",           "Texture yüksekliği. Sarmalayıcı: texture_height()."},
    {"tm_unload_texture", "tm_unload_texture(tex: int)",                "Texture'ı GPU'dan bırakır (close_window zaten hepsini bırakır). Sarmalayıcı: unload_texture()."},
    {"tm_load_font",    "tm_load_font(path: str, size: int): int",      "TTF/OTF fontu verilen boyutta yükler, handle döner (-1 = başarısız). Sarmalayıcı: load_font()."},
    {"tm_text_font",    "tm_text_font(font: int, s: str, x, y, size: int, color: int)", "Özel fontla yazı çizer. Sarmalayıcı: text_font()."},
    {"tm_measure_text", "tm_measure_text(s: str, size: int): int",      "Varsayılan fontla yazının piksel genişliği — ortalamak için. Sarmalayıcı: measure_text()."},
    {"tm_load_sound",   "tm_load_sound(path: str): int",                "WAV/OGG ses efektini yükler, handle döner (-1 = başarısız). Ses aygıtı ilk yüklemede otomatik açılır. Sarmalayıcı: load_sound()."},
    {"tm_play_sound",   "tm_play_sound(snd: int)",                      "Ses efektini çalar (üst üste tetiklenebilir). Sarmalayıcı: play_sound()."},
    {"tm_stop_sound",   "tm_stop_sound(snd: int)",                      "Ses efektini durdurur. Sarmalayıcı: stop_sound()."},
    {"tm_sound_volume", "tm_sound_volume(snd: int, vol: float)",        "Ses seviyesi 0.0-1.0. Sarmalayıcı: sound_volume()."},
    {"tm_load_music",   "tm_load_music(path: str): int",                "Müzik stream'i yükler (WAV/OGG/MP3), handle döner. Çalan müzik frame_end()'de otomatik beslenir — update çağrısı gerekmez. Sarmalayıcı: load_music()."},
    {"tm_play_music",   "tm_play_music(mus: int)",                      "Müziği başlatır. Sarmalayıcı: play_music()."},
    {"tm_stop_music",   "tm_stop_music(mus: int)",                      "Müziği durdurur. Sarmalayıcı: stop_music()."},
    {"tm_music_volume", "tm_music_volume(mus: int, vol: float)",        "Müzik seviyesi 0.0-1.0. Sarmalayıcı: music_volume()."},
    {"tm_triangle",     "tm_triangle(x1, y1, x2, y2, x3, y3, color: int)", "Dolu üçgen çizer (köşe sırası önemsiz — sarım otomatik düzeltilir). Sarmalayıcı: triangle()."},
    {"tm_screenshot",   "tm_screenshot(path: str)",                     "Pencerenin anlık görüntüsünü PNG olarak kaydeder. Dosya her zaman çalışma dizinine yazılır (path'in dizin kısmı kırpılır — raylib davranışı). Sarmalayıcı: screenshot()."},
    {"tm_gamepad_available", "tm_gamepad_available(id: int): bool",     "id numaralı gamepad bağlı mı? (0'dan başlar.) Sarmalayıcı: gamepad_available()."},
    {"tm_gamepad_name", "tm_gamepad_name(id: int): str",                "Gamepad'in adı (bağlı değilse \"\"). Sarmalayıcı: gamepad_name()."},
    {"tm_gamepad_down", "tm_gamepad_down(id: int, btn: str): bool",     "Gamepad butonu basılı mı? Ad: \"A\"/\"B\"/\"X\"/\"Y\" (veya \"CROSS\"/\"CIRCLE\"...), \"UP\"/\"DOWN\"/\"LEFT\"/\"RIGHT\", \"LB\"/\"RB\", \"LT\"/\"RT\", \"START\"/\"SELECT\"/\"GUIDE\", \"L3\"/\"R3\". Sarmalayıcı: gamepad_down()."},
    {"tm_gamepad_pressed", "tm_gamepad_pressed(id: int, btn: str): bool", "Gamepad butonuna bu karede mi basıldı (tek tetik)? Sarmalayıcı: gamepad_pressed()."},
    {"tm_gamepad_axis", "tm_gamepad_axis(id: int, axis: str): float",   "Analog eksen değeri: \"LX\"/\"LY\"/\"RX\"/\"RY\" (-1..1), \"LT\"/\"RT\" tetikler. Sarmalayıcı: gamepad_axis()."},
    {"tm_save_data",    "tm_save_data(name: str, text: str): bool",     "Kalıcı kayıt yazar: Android'de uygulama internal storage'ına, masaüstünde çalışma dizinine. Sarmalayıcı: save_data()/kayit_yaz()."},
    {"tm_load_data",    "tm_load_data(name: str): str",                 "Kalıcı kaydı okur; dosya yoksa \"\" döner. Sarmalayıcı: load_data()/kayit_oku()."},
    {"tm_vibrate",      "tm_vibrate(ms: int)",                          "Cihazı ms milisaniye titretir (Android; masaüstü/web no-op). Sarmalayıcı: vibrate()/titret()."},

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
    {"sleep",        "sleep(ms: int): void",                        "Verilen milisaniye kadar bekler (bloklar)."},
    {"sleep_async",  "sleep_async(ms: int): promise",               "Bloklamayan timer; `await sleep_async(ms)` ile kullanılır. AOT async."},
    {"gather",       "gather(...promises): promise",                "Tüm promise'leri eşzamanlı bekler, sonuçları dizi olarak verir. `let r = await gather(a, b);`"},

    // ---- File ----
    {"read_file",    "read_file(path: str): str",                   "Dosyayı tamamen okur."},
    {"write_file",   "write_file(path: str, data: str): bool",      "Dosyayı yeniden yazar."},
    {"append_file",  "append_file(path: str, data: str): bool",     "Dosyaya ekler."},
    {"file_exists",  "file_exists(path: str): bool",                "Dosya/dizin var mı?"},

    // ---- System ----
    {"sys_run",      "sys_run(cmd: str): int",                      "Kabuk komutunu çalıştırır; çıktı canlı akar, exit code döner (0=başarı)."},
    {"regex_match",  "regex_match(pattern: str, s: str): int",      "TÜM string desene uyuyorsa 1 (std::regex_match; alt-dize için regex_search)."},
    {"regex_search", "regex_search(pattern: str, s: str): int",     "Desen string içinde herhangi bir yerde geçiyorsa 1."},
    {"regex_capture","regex_capture(pattern: str, s: str): array",  "İlk eşleşmenin yakalama gruplarını dizi olarak döner (0 = tüm eşleşme)."},
    {"regex_replace","regex_replace(pattern: str, s: str, repl: str): str", "Desene uyan her parçayı değiştirir; bozuk desende s'i aynen döner."},
    {"read_key",     "read_key(): str",                             "Tek tuş okur (Enter'sız, ekrana yansımadan). Ok tuşları: up/down/left/right; ayrıca enter/esc/space/tab/backspace; diğerleri karakterin kendisi."},
    {"sys_lang",     "sys_lang(): str",                             "İşletim sistemi arayüz dilini küçük harf ISO-639 kodu olarak döner (\"tr\", \"en\", ...). Yerelleştirme için. Belirlenemezse \"\"."},
    {"read_key_timeout", "read_key_timeout(ms: int): str",          "read_key gibi ama en fazla ms milisaniye bekler; süre dolarsa \"\" (boş) döner. Canlı/animasyonlu TUI (spinner, ilerleme, otomatik yenileme) için."},
    {"term_width",   "term_width(): int",                           "Terminal genişliği (sütun sayısı). Belirlenemezse 80. Responsive TUI düzeni için."},
    {"term_height",  "term_height(): int",                          "Terminal yüksekliği (satır sayısı). Belirlenemezse 24. Responsive TUI düzeni için."},
    {"display_width","display_width(s: str): int",                  "s'nin terminaldeki görünür sütun genişliği. ANSI renk kodları 0; geniş/emoji 2; birleşen işaretler 0; UTF-8 farkında. length()'in aksine hizalama için doğru."},
    {"fit_width",    "fit_width(s: str, width: int): str",          "s'yi tam olarak width sütuna oturtur: uzunsa kod-noktası sınırında keser ve … ekler, kısaysa boşlukla sağa doldurur. TUI kolonları için."},
    {"screen_open",  "screen_open(): void",                         "TUI için alt-ekrana geçer, imleci gizler, satır kaydırmayı kapatır ve ekranı temizler. Ham ANSI yazmadan tam ekran uygulama başlatır."},
    {"screen_close", "screen_close(): void",                        "screen_open()'ın tersi: normal ekrana döner, imleci ve satır kaydırmayı geri açar."},
    {"screen_render","screen_render(frame: str): void",             "Bir kareyi titremesiz (senkronize çıktı) ve kaymadan atomik olarak çizer. Uygulama hiç escape kodu yazmaz; kareyi normal string olarak kurar."},
    {"style",        "style(s: str, spec: str): str",               "s'yi ANSI stilleriyle sarar. spec boşlukla ayrılmış: bold dim italic underline invert; renk adları (red green yellow blue magenta cyan white gray); bright-<renk>; arka plan için on-<renk>. Ham escape yerine okunur isimler."},

    // ---- DB ----
    {"db_open",      "db_open(path: str): int",                     "SQLite veritabanı açar."},
    {"db_close",     "db_close(handle: int): void",                 ""},
    {"db_execute",   "db_execute(handle: int, sql: str, params?: array): bool",      "INSERT/UPDATE/DELETE yürütür. Opsiyonel params dizisi ? yer tutucularına bağlanır (injection-safe)."},
    {"db_query",     "db_query(handle: int, sql: str, params?: array): array<json>", "SELECT döner. Opsiyonel params dizisi ? yer tutucularına bağlanır (injection-safe)."},
    {"db_last_insert_id", "db_last_insert_id(handle: int): int",    ""},
    {"db_error",     "db_error(handle: int): str",                  "Son hatayı döner."},

    // ---- HTTP ----
    {"http_parse_request",  "http_parse_request(raw: str): json",   "Ham HTTP isteğini parçalar."},
    {"http_create_response","http_create_response(status: int, ct: str, body: str): str", "HTTP yanıtı oluşturur."},
    {"http_status_text",    "http_status_text(code: int): str",     "200→\"OK\", 404→\"Not Found\", …"},
    {"http_request",        "http_request(method: str, url: str, body: str, headers?: json): json", "Bloklayan outbound HTTP isteği → {ok, status, headers, body}. Opsiyonel 4. arg {name: value} ek istek başlıkları (örn. Authorization, Accept) gönderir."},
    {"http_request_async",  "http_request_async(method: str, url: str, body: str): promise", "Bloklamayan outbound HTTP; promise döner. `await http_request_async(...)`. Worker pool (TULPAR_HTTP_POOL)."},
    {"path_match",          "path_match(pattern: str, path: str): json", "/users/:id ile gelen path'i eşler."},
    {"parse_query",         "parse_query(qs: str): json",           "?a=1&b=2 → {a: \"1\", b: \"2\"}"},
    {"parse_multipart",     "parse_multipart(body: str, content_type: str): json", "multipart/form-data → {fields, files}"},
    {"parse_cookies",       "parse_cookies(header: str): json",     "Cookie header'ı parse eder: \"a=1; b=2\" → {a: \"1\", b: \"2\"}"},

    // ---- Wings server (lib/wings.tpr) — import \"wings\" ----
    {"get",                 "get(path: str, handler): void",        "GET route kaydı. Handler'ı adıyla bağlayın: get(\"/users\", list_users)."},
    {"post",                "post(path: str, handler): void",       "POST route kaydı."},
    {"put",                 "put(path: str, handler): void",        "PUT route kaydı."},
    {"del",                 "del(path: str, handler): void",        "DELETE route kaydı."},
    {"serve",               "serve(port?: int, workers?: int): void", "Sunucuyu başlatır: serve() → 8484, serve(8080) → açık port, serve(8080, 4) → 4 worker'lı pool. /healthz, /metrics, /docs önayarlı."},
    {"ok",                  "ok(data: json): json",                 "200 OK yanıt helper'ı."},
    {"created",             "created(data: json): json",            "201 Created yanıt helper'ı."},
    {"no_content",          "no_content(): json",                   "204 No Content."},
    {"bad_request",         "bad_request(msg: str): json",          "400 Bad Request — {error: msg}."},
    {"unauthorized",        "unauthorized(msg: str): json",         "401 Unauthorized."},
    {"forbidden",           "forbidden(msg: str): json",            "403 Forbidden."},
    {"not_found",           "not_found(msg: str): json",            "404 Not Found — {error: msg}."},
    {"conflict",            "conflict(msg: str): json",             "409 Conflict."},
    {"server_error",        "server_error(msg: str): json",         "500 Internal Server Error."},
    {"text",                "text(body: str): json",                "text/plain yanıt zarfı."},
    {"with_status",         "with_status(data: json, status: int): json", "Herhangi bir HTTP durum kodu ile yanıt."},
    {"persist",             "persist(value): value",                "Bir değeri kalıcı belleğe derin kopyalar (arena reset'ten sağ çıkar). In-memory global'lerde sakla: push(_users, persist(u))."},

    // ---- Wings DX katmanı (WINGS_DX.md) — kısa isimler + resource ----
    {"resource",            "resource(path: str, model: json, opts?: json): void", "ORM model handle'ından otomatik REST CRUD: GET/POST path, GET/PUT/DELETE path/:id + türetilmiş body_schema (422) + /docs. opts: {\"only\": [...]} / {\"except\": [...]}."},
    {"delete",              "delete(path: str, handler): void",     "DELETE route kaydı (del ile aynı; HTTP fiiliyle birebir isim)."},
    {"patch",               "patch(path: str, handler): void",      "PATCH route kaydı."},
    {"head",                "head(path: str, handler): void",       "HEAD route kaydı (GET gövdesiz)."},
    {"options",             "options(path: str, handler): void",    "OPTIONS route kaydı."},
    {"accepts",             "accepts(schema: json): void",          "Son route'a istek gövdesi şeması bağlar (body_schema aliası); uymayan gövde handler'a girmeden 422."},
    {"returns",             "returns(schema: json): void",          "Son route'a cevap şeması bağlar (response_model aliası); listelenmeyen alanlar cevaptan düşer."},
    {"body_schema",         "body_schema(schema: json): void",      "Son route'a istek gövdesi şeması bağlar: {\"name\": \"str\", \"age?\": \"int\"} → uymayan gövde 422."},
    {"response_model",      "response_model(schema: json): void",   "Son route'a cevap şeması bağlar; sadece listelenen alanlar serialize edilir (sır sızdırmaz)."},
    {"cookies",             "cookies(req: json): json",             "İstek çerezlerini dict olarak okur. UFCS: req.cookies()."},
    {"gzip",                "gzip(min_bytes?: int): void",          "Yanıt gzip sıkıştırmasını açar (enable_gzip aliası); eşik altı gövdeler dokunulmaz."},
    {"ws_upgrade",          "ws_upgrade(req: json): json",          "WebSocket el sıkışmasını tamamlar (wings_ws_upgrade aliası)."},
    {"ws_send",             "ws_send(fd: int, payload: str): int",  "WebSocket text frame gönderir (wings_ws_send_text aliası)."},
    {"ws_close",            "ws_close(fd: int): int",               "WebSocket close frame gönderir."},
    {"ws_pong",             "ws_pong(fd: int, payload: str): int",  "WebSocket pong frame gönderir."},
    {"sse_headers",         "sse_headers(): str",                   "SSE stream başlık bloğu (text/event-stream) döner; socket_send ile yaz."},
    {"sse_event",           "sse_event(name: str, data: str): str", "Tek SSE event frame'i formatlar; name boşsa default message event'i."},
    {"metrics_prom",        "metrics_prom(): str",                  "Prometheus text formatında sunucu metrikleri (wings_metrics_prom aliası)."},
    {"param",               "param(req: json, name: str, fallback: str): str", "Path parametresi okur: req.param(\"id\", \"\"). Tipli: param_int / param_bool."},
    {"query",               "query(req: json, name: str, fallback: str): str", "Query-string parametresi okur: req.query(\"q\", \"\"). Tipli: query_int / query_bool."},
    {"form",                "form(req: json, name: str, fallback: str): str", "Form alanı okur (urlencoded/multipart): req.form(\"name\", \"\")."},

    // ---- ORM v2 (lib/orm.tpr) — import \"orm\", model handle + UFCS ----
    {"database",            "database(path: str): int",             "SQLite dosyasını açar; sonraki model() çağrılarının varsayılan bağlantısı olur. import \"orm\"."},
    {"model",               "model(table: str, schema: json): json", "Tabloyu (yoksa) oluşturur, model handle döner: model(\"notes\", {\"id\": \"pk\", \"title\": \"str!\", \"done\": \"bool\"}). UFCS ile kullan: Note.find(1)."},
    {"find",                "find(m: json, id: int): json",         "id ile tek satır (tipler cast'li) ya da {} — UFCS: Note.find(1). import \"orm\"."},
    {"all",                 "all(m: json): array",                  "Tablodaki tüm satırlar (cast'li) — Note.all()."},
    {"where",               "where(m: json, cond: str, params: array): array", "Bağlı parametreli filtre: Note.where(\"done = ? AND id > ?\", [0, 5]). Değerler asla SQL'e gömülmez."},
    {"first",               "first(m: json, cond: str, params: array): json", "İlk eşleşen satır ya da {} — Note.first(\"done = ?\", [0])."},
    {"count",               "count(m: json): int",                  "Tablodaki satır sayısı — Note.count()."},
    {"create",              "create(m: json, attrs: json): int",    "Parametreli INSERT; yeni kaydın id'sini döner. Şema dışı anahtarlar düşer; 0/\"\"/false değerler de yazılır."},
    {"update",              "update(m: json, id: int, attrs: json): int", "Parametreli UPDATE (id üstünden). 0/\"\"/false dâhil attrs'taki her şemalı anahtar yazılır."},
    {"save",                "save(m: json, obj: json): json",       "Upsert: obj \"id\" taşıyor ve kayıt varsa update, yoksa create; taze satırı döner."},
    {"remove",              "remove(m: json, id: int): int",        "id ile satır siler — Note.remove(3)."},
    {"raw",                 "raw(m: json, where_sql: str): array",  "HAM where parçasıyla SELECT — parametre bağlanmaz, içine kullanıcı girdisi GÖMME. Kaçış kapısı."},

    // ---- Crypto / encoding ----
    {"password_hash",       "password_hash(password: str): str",            "Şifreyi PBKDF2-HMAC-SHA256 ile hash'ler (kendini tanımlayan `pbkdf2_sha256$iters$salt$dk` string). Auth için sha256 yerine bunu kullan."},
    {"password_verify",     "password_verify(password: str, stored: str): bool", "Şifreyi password_hash çıktısına karşı sabit-zamanlı doğrular."},
    {"hmac_sha256",         "hmac_sha256(key: str, msg: str): str",         "HMAC-SHA256 (RFC 2104) — anahtarlı MAC, 64-karakter hex. JWT HS256 / imzalı çerez / webhook imzalama yapı taşı. Doğrulama: yeniden hesaplayıp sabit-zamanlı karşılaştır."},
    {"secure_token",        "secure_token(n: int): str",                    "Kriptografik olarak güvenli, n karakterlik base62 rastgele string (CSPRNG / std::random_device). Oturum token'ları, tuzlar vb. için randint yerine bunu kullan."},
    {"gzip_compress",       "gzip_compress(s: str): str",                   "Girdi baytlarını gzip (RFC 1952) akışına sıkıştırır — ağaç-içi DEFLATE, zlib bağımlılığı yok. İkili-güvenli (NUL içerir); wings yanıt sıkıştırmasının yapı taşı."},
    {"sha1",                "sha1(s: str): str",                    "20-baytlık ikili SHA-1 özeti döner."},
    {"sha1_hex",            "sha1_hex(s: str): str",                "40 karakter küçük-harf hex SHA-1."},
    {"base64_encode",       "base64_encode(s: str): str",           "Bayt dizisini base64'e çevirir (padding `=` ile)."},
    {"base64_decode",       "base64_decode(s: str): str",           "Base64'ten bayt dizisine. Hatalı girdi → boş str."},
    {"wings_ws_accept_key", "wings_ws_accept_key(client_key: str): str", "RFC 6455 §4.2.2 handshake: base64(sha1(key + GUID))."},
    {"wings_ws_send_frame", "wings_ws_send_frame(fd: int, opcode: int, payload: str): int", "WebSocket frame yazar (FIN=1, unmasked); 1=text, 2=binary, 8=close, 9=ping, 10=pong."},
    {"wings_ws_recv_frame", "wings_ws_recv_frame(fd: int): json",    "WebSocket frame okur, masking key uygular. {ok, opcode, fin, payload} ya da {ok=0, error}."},
    {"wings_set_current_fd","wings_set_current_fd(fd: int): int",   "Wings dispatcher dahili: handler'a aktif istek fd'sini geçirir."},
    {"wings_current_fd",    "wings_current_fd(): int",              "Aktif istek soketinin fd'si. SSE / WS upgrade streaming için."},

    // ---- Socket ----
    {"socket_server",  "socket_server(host: str, port: int): int",  ""},
    {"socket_client",  "socket_client(host: str, port: int): int",  ""},
    {"socket_accept",  "socket_accept(server_fd: int): int",        ""},
    {"socket_send",    "socket_send(client_fd: int, data: str): int", ""},
    {"socket_receive", "socket_receive(client_fd: int, size: int): str", ""},
    {"socket_close",   "socket_close(fd: int): void",               ""},
    {"socket_peer_ip", "socket_peer_ip(fd: int): str",              "Kabul edilen bağlantının uzak (client) IP'sini döner; hata olursa \"\"."},

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
    {"StringBuilder","StringBuilder(capacity: int): int",           "Yeni StringBuilder yaratır, handle döner. sb_append/sb_tostring/sb_free ile kullanılır."},
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
