// L6 BRIDGE — Tulpar betikleri icin motorun C ABI yuzu (PLAN L5: oyun mantigi
// Tulpar'da, ayni ikilide, script siniri yok). Duz skalerler (int / double /
// const char*), struct yok, callback yok: Tulpar'in bugunku FFI'si bunu tasir.
// Tulpar tarafi: runtime/engine_bindings.cpp (aot_eng_*_ptr, VMValue ABI) bu
// fonksiyonlari cagirir; lib/engine.tpr ergonomik sarmalayicidir.
//
// Tek ornek (global motor baglami): teng_init -> [teng_frame_begin ... teng_frame_end]* -> teng_shutdown.
// Her cagri loglanir (TULPAR_ENGINE_LOG=0 sessiz, 1 bilgi (vars.), 2 ayrinti, 3 iz: her cagri);
// hata her seviyede basilir ve son 64 satirlik halka `teng_shutdown`/cokmede dokulur.
// Basliksiz kip: TULPAR_ENGINE_HEADLESS=N (N kare, offscreen) + TULPAR_ENGINE_OUT=x.ppm.
//
// Kimlikler: varlik id'si nesil etiketli tamsayi ((nesil<<16)|yuva), 0 = gecersiz.
// Renkler: paketlenmis 0xRRGGBBAA (tame ile ayni), yazar rengi sRGB.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- yasam dongusu ---------------------------------------------------------
int teng_init(const char *title, int width, int height); // 1 basari
int teng_running(void);
void teng_close(void);
void teng_shutdown(void);
int teng_frame_begin(void); // girdi + zaman; 0 = pencere yok (arka plan), yine de frame_end cagrilir
void teng_frame_end(void);  // sim (sabit adim) + cizim + sunum
double teng_dt(void);
double teng_time(void);
int teng_frame(void);
double teng_fps(void);
int teng_width(void);
int teng_height(void);
int teng_headless(void); // 1: pencere yok (TULPAR_ENGINE_HEADLESS)
void teng_set_headless(int frames, const char *out_ppm); // init'ten once; env'i ezer
void teng_log(const char *msg);      // betikten log: [tpr] onekiyle ayni akisa
void teng_log_level(int level);      // 0..3
int teng_screenshot(const char *path); // son kare PPM (yalniz headless/offscreen); 1 basari
const char *teng_gpu_name(void);
const char *teng_last_error(void);
int teng_error_count(void);   // loglanan HATA sayisi (dogrulama kapilari icin)
int teng_warning_count(void); // loglanan UYARI sayisi

// --- dunya / kamera --------------------------------------------------------
void teng_gravity(double gx, double gy, double gz); // init'ten once etkili
// Parlama (bloom): init'ten ONCE acilir; esik/yogunluk kare icinde de degisir.
void teng_bloom(int enable, double threshold, double intensity);
int teng_bloom_on(void);
void teng_sun(double dx, double dy, double dz, double diffuse);
void teng_ambient(int64_t color);
void teng_shadow_volume(double cx, double cy, double cz, double radius, double depth);
void teng_camera(double ex, double ey, double ez, double tx, double ty, double tz);
void teng_camera_orbit(double tx, double ty, double tz, double yaw, double pitch, double radius);
double teng_camera_x(void);
double teng_camera_y(void);
double teng_camera_z(void);

// --- derlenmis sahne (.sahneb) ----------------------------------------------
int teng_scene_load(const char *path); // 1 basari; govdeler fizige girer, dunya ayarlari uygulanir
int teng_scene_unload(void);           // govdeleri fizikten cikarir, cizimi durdurur; sonra yeniden yuklenebilir (bolum gecisi)
int teng_scene_loaded(void);
int teng_scene_count(void);
int teng_scene_find(const char *name); // -1 yok
double teng_scene_x(int i);
double teng_scene_y(int i);
double teng_scene_z(int i);
const char *teng_scene_name(int i);
// Sahne govdeleri okunur DEGIL, itilebilir de: dinamik olmayan varlikta hata
// loglanir ve cagri yok sayilir (sessiz yutma yok).
double teng_scene_vx(int i);
double teng_scene_vy(int i);
double teng_scene_vz(int i);
int teng_scene_is_dynamic(int i);
void teng_scene_set_velocity(int i, double vx, double vy, double vz);
void teng_scene_impulse(int i, double ix, double iy, double iz); // hiz += i

// --- varliklar (kopru sahibi) ------------------------------------------------
int teng_spawn_box(double x, double y, double z, double hx, double hy, double hz, int dynamic, int64_t color);
int teng_spawn_sphere(double x, double y, double z, double radius, int dynamic, int64_t color);
int teng_spawn_ground(double half_size, int64_t color); // dama dokulu duzlem + ince sabit kutu govde
int teng_load_model(const char *path);                  // glTF; -1 hata
int teng_spawn_model(int asset, double x, double y, double z, double scale, int64_t tint);
int teng_spawn_light(double x, double y, double z, int64_t color, double intensity, double radius);
void teng_despawn(int id);
int teng_alive(int id);
int teng_count(void);
double teng_x(int id);
double teng_y(int id);
double teng_z(int id);
void teng_set_pos(int id, double x, double y, double z); // dinamik govde: govde yeniden kurulur (hiz sifir)
void teng_set_color(int id, int64_t color);
void teng_set_scale(int id, double s);
void teng_set_yaw(int id, double yaw_deg); // gorsel donus (dinamik govdede sim ezer)
double teng_vx(int id);
double teng_vy(int id);
double teng_vz(int id);
void teng_set_velocity(int id, double vx, double vy, double vz);
void teng_impulse(int id, double ix, double iy, double iz); // hiz += i
int teng_is_dynamic(int id);
int teng_awake(int id); // govde aktif mi (uyuyorsa 0)

// --- model animasyonu ---------------------------------------------------------
// Klip = glTF animasyonu (modele ait). Varliga klip atanirsa her kare poz
// degerlendirilir ve iskeletli cizim yapilir; atanmazsa model statik cizilir.
int teng_model_clip_count(int asset);
double teng_model_clip_duration(int asset, int clip);
const char *teng_model_clip_name(int asset, int clip);
void teng_set_anim(int id, int clip, double speed, int loop); // clip < 0: animasyonu kapat (statik)
double teng_anim_time(int id);                                 // klip icindeki an (s)
int teng_anim_done(int id);                                    // dongusuz klip bitti mi

// --- girdi -----------------------------------------------------------------
int teng_key_down(const char *name);    // "W","A","S","D","SPACE","UP","LEFT","ESC","ENTER","SHIFT", "0".."9"
int teng_key_pressed(const char *name); // bu karede basildi
int teng_touch_count(void);
double teng_touch_x(int i);
double teng_touch_y(int i);
double teng_stick_x(void); // sanal joystick -1..1 (sol yarim ekran surukleme)
double teng_stick_y(void);
int teng_stick_action(void); // sag yarim kisa dokunus (bu karede)
double teng_look_dx(void);   // sag yarim surukleme, piksel/kare
double teng_mouse_x(void);
double teng_mouse_y(void);
int teng_mouse_down(int button);

// --- 2B arayuz (kare icinde cagrilir, frame_end'de cizilir) ------------------
void teng_text(const char *s, double x, double y, double scale, int64_t color);
void teng_rect(double x, double y, double w, double h, int64_t color);
double teng_text_width(const char *s, double scale);

// --- anlik-kip (immediate mode) arayuz: menu / ayar / duraklat ---------------
// Motorda YENI cizim yolu YOK: her sey teng_rect + teng_text uzerine kurulur.
// Durum koprude sabit kapasiteli dizide (kimlik = etiket ozeti), kare icinde
// AYIRMA YOK. Deger sahipligi BETIKTEDIR (Tulpar'da isaretci/cikti parametresi
// yok): onay kutusu ve kaydirici YENI degeri DONDURUR, betik geri yazar.
// Sira: teng_ui_begin -> widget'lar -> teng_ui_end, hepsi kare icinde.
// Isaretci: enjeksiyon > dokunmatik (parmak 0) > fare (sol tus).
void teng_ui_begin(void);
void teng_ui_end(void);
void teng_ui_theme(int64_t panel, int64_t text, int64_t accent);
void teng_ui_enable(int on); // sonraki widget'lar etkin mi (kapaliyken deger DEGISMEZ)
void teng_ui_panel(double x, double y, double w, double h, int64_t color);
void teng_ui_label(const char *s, double x, double y, double scale, int64_t color);
int teng_ui_button(const char *label, double x, double y, double w, double h);     // 1: bu karede tiklandi
int teng_ui_checkbox(const char *label, double x, double y, double w, double h, int value); // YENI deger
double teng_ui_slider(const char *label, double x, double y, double w, double h, double value, double min_v, double max_v); // YENI deger
int teng_ui_active(void);  // bir widget basili/surukleniyor (oyun girdisini bastir)
int teng_ui_clicks(void);  // toplam etkinlestirme (dugme tiklamasi + onay kutusu degisimi)
// Pencersiz kipte fare/dokunmatik yoktur: betik tek karelik bas+birak enjekte
// eder (sonraki teng_ui_begin tuketir). Dugme disina tiklama tetiklemez.
void teng_ui_test_click(double x, double y);

// --- kalici kayit (anahtar-deger): ayarlar + en yuksek skor -------------------
// Motor KURULMADAN da calisir: parlama gibi ayarlar teng_init'ten ONCE okunur.
// Yol: TULPAR_ENGINE_SAVE, yoksa calisma dizininde "tulpar_kayit.txt"
// (Android'de calisma dizini APK cikarma kokudur: uygulamanin yazilabilir
// dahili dizini). Bicim: satir basina "anahtar=deger", '#' yorum. Bozuk satir
// SESSIZCE YUTULMAZ: hata loglanir, sayac artar, kalan satirlar okunur.
int teng_save_open(const char *path); // depoyu bu yola baglar + okur; 1 dosya okundu, 0 dosya yok (yeni kayit)
const char *teng_save_path(void);
void teng_save_set(const char *key, double value);
double teng_save_get(const char *key, double def); // anahtar yoksa def (hata degil); sayisal degilse HATA + def
void teng_save_set_str(const char *key, const char *value);
const char *teng_save_get_str(const char *key, const char *def);
int teng_save_has(const char *key);
int teng_save_write(void); // diske yazar (gecici dosya + rename); teng_shutdown kirli kaydi kendi yazar
void teng_save_clear(void);
int teng_save_count(void);

// --- sahne sicak yeniden yukleme ----------------------------------------------
// Editorde "Derle" -> .sahneb degisir -> oyun kendini yeniler ("The Truth"
// dongusunun oyun ayagi). Dosyanin degisim zamani/boyutu izlenir; degisim
// GORULDUKTEN sonra bir kontrol daha beklenir (yarim yazilmis blob yuklenmesin).
void teng_scene_watch(int on);
int teng_scene_reloaded(void);     // son okumadan beri yeniden yuklendi mi (BIR KEZ true, tuketilir)
int teng_scene_reload_count(void); // toplam sicak yukleme sayisi
const char *teng_scene_path(void); // yuklu (ya da izlenen) sahne dosyasi
// Ikili dosya kopyalama / degisim zamani: .sahneb gibi NUL iceren bloblar
// Tulpar string'ine sigmaz; arac ve test betikleri icin.
int teng_file_copy(const char *src, const char *dst);
double teng_file_mtime(const char *path); // saniye (epoch), 0 = dosya yok

// --- ses -------------------------------------------------------------------
// Cihaz betigin istegiyle acilir (acilmazsa oyun CALISMAYA DEVAM eder: hata
// loglanir, butun ses cagrilari sessizce yok sayilir). TULPAR_ENGINE_AUDIO_NULL=1
// null arka ucu zorlar (test/pozitif kontrol: callback kosar, hoparlore gitmez).
int teng_audio_open(int sample_rate, int channels); // 1 basari
void teng_audio_close(void);
int teng_audio_ok(void);
const char *teng_audio_backend(void); // "pulseaudio 'Speakers' 48000 Hz" gibi; kapaliysa ""
int teng_audio_load(const char *path); // WAV/FLAC/MP3 -> klip tutamaci, -1 hata
int teng_audio_tone(double hz, double seconds); // sentetik sinus klibi (ayni parametre = ayni tutamac)
int teng_audio_play(int clip, double gain, int loop); // ses id'si (0 hata)
int teng_audio_beep(double hz, double seconds, double gain); // ton + cal, tek cagri
void teng_audio_stop(int voice);
void teng_audio_stop_all(void);
void teng_audio_master(double gain); // ana ses seviyesi 0..1 (calan sesler de guncellenir)
int teng_audio_playing(void);        // cihaz thread'inin olctugu aktif ses sayisi
double teng_audio_peak(void);        // son render'daki en buyuk |ornek| (0 = sessiz)

// --- sorgular: isin testi ve yakinlik ---------------------------------------
// Isin testi Jolt'un dar faz sorgusudur: govdeler eklendikten SONRA en az bir
// teng_frame_end gerekir (genis faz agaci orada guncellenir), yoksa yeni govde
// bulunmayabilir. Sorgu salt okunur: sim durumunu DEGISTIRMEZ.
// skip_id: isin kendi govdesinden baslarken (zemin kontrolu) onu atlar; Jolt
// sorgusunda filtre olmadigi icin kopru carpma noktasinin bir tik otesinden
// yeniden atar (en cok 4 deneme). 0 = atlama yok.
// Donus: carpma mesafesi (m), -1 = iska. Ayrinti son sorguda saklanir.
double teng_raycast(double ox, double oy, double oz, double dx, double dy, double dz, double max_dist, int skip_id);
double teng_ray_x(void); // son carpma noktasi
double teng_ray_y(void);
double teng_ray_z(void);
double teng_ray_nx(void); // son carpma yuzey normali (duvar boyunca kayma)
double teng_ray_ny(void);
double teng_ray_nz(void);
int teng_ray_id(void);    // carpilan KOPRU varligi (0 = kopru varligi degil)
int teng_ray_scene(void); // carpilan SAHNE varligi dizini (-1 = sahne govdesi degil)
// Kure sorgusu: merkeze `radius` icinde kalan kopru varliklari, YAKINDAN UZAGA
// sirali. Zemin (dev govde, her sorguyu doldururdu) ve isik (govdesi yok)
// disaridadir; sahne varliklari da disaridadir (onlar teng_scene_* ile okunur).
// Olcut: merkez uzakligi <= radius + varligin sinir yaricapi.
int teng_overlap(double x, double y, double z, double radius, int skip_id); // sonuc sayisi
int teng_overlap_id(int i);
double teng_overlap_dist(int i);
int teng_nearest(double x, double y, double z, double radius, int skip_id); // en yakin id (0 yok)

// --- Carpisma olaylari -------------------------------------------------------
// Bugunku FFI callback TASIMIYOR (duz skaler ABI), o yuzden carpisma bir geri
// cagrim degil KUYRUK: fizik adiminda olusan temaslar halkaya yazilir, oyun
// kare icinde okur. Halka her kare basinda (teng_frame_begin) temizlenir, yani
// olaylar "bu karede olanlar"dir; bir karede birden cok fizik adimi atilsa da
// hepsi birikir.
//
// Sessiz kirpilma YOK: halka dolarsa teng_collision_dropped() artar. Oyun bunu
// okuyup kapasiteyi buyutmeli ya da olayi daha erken tuketmeli — "carpma
// gelmedi" sanmak en kotu sonuctur.
int teng_collision_count(void);      // bu karedeki olay sayisi
int teng_collision_dropped(void);    // halkaya sigmayip DUSEN olay sayisi
int teng_collision_a(int i);         // taraf A: kopru varlik id'si (0 = kopru varligi degil)
int teng_collision_b(int i);         // taraf B
int teng_collision_scene_a(int i);   // taraf A sahne varlik dizini (-1 = degil)
int teng_collision_scene_b(int i);
double teng_collision_x(int i);      // temas noktasi (dunya)
double teng_collision_y(int i);
double teng_collision_z(int i);
double teng_collision_nx(int i);     // A'dan B'ye yuzey normali
double teng_collision_ny(int i);
double teng_collision_nz(int i);
double teng_collision_speed(int i);  // temas noktasinda normal boyu goreli hiz (m/s) = carpma siddeti

// --- navmesh (sahne blob'undaki bake; runtime yalniz SORGULAR) ---------------
// Veri engine_sahnec'in Recast bake'i: sahnedeki SABIT KUTU govdelerden. Sahnede
// yurunebilir zemin yoksa bake olmaz -> teng_nav_ok() 0 ve oyun duz yola duser.
int teng_nav_ok(void);
int teng_nav_polys(void);
int teng_nav_path(double fx, double fy, double fz, double tx, double ty, double tz); // nokta sayisi (0 yol yok)
int teng_nav_partial(void); // son yol kismi mi (hedefe ulasilamadi)
double teng_nav_x(int i);
double teng_nav_y(int i);
double teng_nav_z(int i);
// Navmesh'e en yakin nokta: oyuncunun/ajanin konumu mesh'in birkac santim
// disindaysa teng_nav_path 0 doner. Once buraya yapistirip yolu oradan iste.
// 1 = bulundu (teng_nav_near_x/y/z okunur), 0 = arama kutusunda mesh yok.
int teng_nav_nearest(double x, double y, double z);
double teng_nav_near_x(void);
double teng_nav_near_y(void);
double teng_nav_near_z(void);
// Navmesh uzerinde dogru gorus. 1 = ENGEL VAR (yol arama gerekir), 0 = temiz
// (dumduz gidilebilir). Carpma parametresi teng_nav_ray_t() ile okunur (0..1;
// engel yoksa 1). Yol aramadan cok daha ucuz — "string pulling" icin.
int teng_nav_raycast(double fx, double fy, double fz, double tx, double ty, double tz);
double teng_nav_ray_t(void);

// --- olcum -----------------------------------------------------------------
int teng_draw_count(void);
int teng_body_count(void);
int teng_light_count(void);
double teng_frame_ms(void); // son kare p50 (profiler)

#ifdef __cplusplus
}
#endif
