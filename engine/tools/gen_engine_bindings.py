#!/usr/bin/env python3
"""eng_* builtin ailesinin TEK kaynagi: bu tablo -> 4 uretilmis dosya.

  runtime/engine_bindings.cpp          aot_eng_*_ptr (VMValue ABI) -> teng_* (C ABI)
  src/aot/engine_builtins_table.inc    LLVM backend tablosu (ad, sembol, arite)
  src/typeinfer/engine_builtins_sigs.inc  tip cikarimi imzalari
  src/lsp/engine_builtins.inc          LSP tamamlama/hover

Neden uretiliyor: "5 noktada baglama" (CLAUDE.md) elle yapilinca noktalar
birbirinden kayiyor (typeinfer'da eksik imza = denetimsiz cagri; LSP'de eksik
= tamamlama yok). Tek tablo, tek komut: `python3 engine/tools/gen_engine_bindings.py`.
Uretilen dosyalar depoya girer (build'de python sart olmasin); degistirmek icin
BU dosyayi duzenle ve yeniden uret.

Parametre tipleri: num (int ya da float, koordinat/olcu), int, color (0xRRGGBBAA int),
str, flag (bool ya da int). Donus: void, bool, int, float, str.
"""
import os
import sys

# (ad, donus, [(param, tip), ...], aciklama)
SPEC = [
    # yasam dongusu
    ("eng_init", "bool", [("title", "str"), ("w", "int"), ("h", "int")], "Motoru kurar: pencere (ya da TULPAR_ENGINE_HEADLESS=N ile pencersiz N kare), Vulkan, fizik. Basarisizsa false (eng_last_error)."),
    ("eng_running", "bool", [], "Oyun dongusu surmeli mi (pencere kapanmadi, eng_close cagrilmadi, headless kare siniri dolmadi)."),
    ("eng_close", "void", [], "Dongunun bir sonraki kontrolde bitmesini ister."),
    ("eng_shutdown", "void", [], "Motoru kapatir; hata varsa log halkasini doker."),
    ("eng_frame_begin", "bool", [], "Kareyi baslatir: girdi, zaman. false = pencere yok (arka plan), yine de eng_frame_end cagir."),
    ("eng_frame_end", "void", [], "Kareyi bitirir: fizik (sabit adim), cizim, sunum."),
    ("eng_dt", "float", [], "Son kare suresi (s)."),
    ("eng_time", "float", [], "Kurulumdan beri gecen sure (s)."),
    ("eng_frame", "int", [], "Kare sayaci."),
    ("eng_fps", "float", [], "Kare hizi (p50, 120 karede bir guncellenir)."),
    ("eng_width", "int", [], "Gorunen genislik (piksel)."),
    ("eng_height", "int", [], "Gorunen yukseklik (piksel)."),
    ("eng_headless", "bool", [], "Pencersiz kipte mi."),
    ("eng_set_headless", "void", [("frames", "int"), ("out_ppm", "str")], "eng_init'ten once: pencersiz N kare, son kare PPM dosyasina."),
    ("eng_log", "void", [("msg", "str")], "Motor log akisina [tpr] onekiyle satir yazar (logcat/stdout + halka)."),
    ("eng_log_level", "void", [("level", "int")], "Log seviyesi: 0 sessiz, 1 bilgi, 2 ayrinti, 3 iz (her cagri)."),
    ("eng_screenshot", "bool", [("path", "str")], "Son kareyi PPM olarak yazar (yalniz headless)."),
    ("eng_gpu_name", "str", [], "Vulkan cihazinin adi."),
    ("eng_last_error", "str", [], "Son kurulum hatasi metni."),
    ("eng_error_count", "int", [], "Motorun logladigi HATA sayisi (gecersiz id, kare disi cizim, kaynak yuklenemedi...). Oyun sonunda 0 bekle."),
    ("eng_warning_count", "int", [], "UYARI sayisi (pencere yok -> headless, font bulunamadi...)."),
    # dunya / kamera
    ("eng_bloom", "void", [("enable", "flag"), ("threshold", "num"), ("intensity", "num")], "Parlama (bloom): eng_init'ten ONCE acilir (ic HDR hedefi kurulur). Esik DOGRUSAL parlaklik (1.0 = yalniz cok parlak yerler), yogunluk 0 = kapali. Kare icinde esik/yogunluk degistirilebilir."),
    ("eng_bloom_on", "bool", [], "Parlama acik mi (HDR bicimi yoksa motor kapatir; sebep logda)."),
    ("eng_gravity", "void", [("gx", "num"), ("gy", "num"), ("gz", "num")], "Yercekimi (eng_init'ten once)."),
    ("eng_sun", "void", [("dx", "num"), ("dy", "num"), ("dz", "num"), ("diffuse", "num")], "Gunes yonu (normalize edilir) ve siddeti."),
    ("eng_ambient", "void", [("color", "color")], "Ortam isigi rengi (0xRRGGBBAA)."),
    ("eng_shadow_volume", "void", [("cx", "num"), ("cy", "num"), ("cz", "num"), ("radius", "num"), ("depth", "num")], "Golge haritasinin kapsadigi hacim (merkez, yaricap, derinlik)."),
    ("eng_camera", "void", [("ex", "num"), ("ey", "num"), ("ez", "num"), ("tx", "num"), ("ty", "num"), ("tz", "num")], "Kamera: goz ve hedef."),
    ("eng_camera_orbit", "void", [("tx", "num"), ("ty", "num"), ("tz", "num"), ("yaw", "num"), ("pitch", "num"), ("radius", "num")], "Yorunge kamerasi: hedef, yaw/pitch (radyan), uzaklik."),
    ("eng_camera_x", "float", [], "Kamera gozu x."),
    ("eng_camera_y", "float", [], "Kamera gozu y."),
    ("eng_camera_z", "float", [], "Kamera gozu z."),
    # sahne blob
    ("eng_scene_load", "bool", [("path", "str")], "Derlenmis sahneyi (.sahneb, engine_sahnec) yukler: modeller, isiklar, govdeler, dunya ayarlari."),
    ("eng_scene_count", "int", [], "Sahne varligi sayisi."),
    ("eng_scene_find", "int", [("name", "str")], "Sahne varliginin dizini, yoksa -1."),
    ("eng_scene_x", "float", [("i", "int")], "Sahne varliginin x'i (dinamikse sim'den)."),
    ("eng_scene_y", "float", [("i", "int")], "Sahne varliginin y'si."),
    ("eng_scene_z", "float", [("i", "int")], "Sahne varliginin z'si."),
    ("eng_scene_name", "str", [("i", "int")], "Sahne varliginin adi."),
    ("eng_scene_unload", "bool", [], "Sahneyi bosaltir: govdeler fizikten cikar, cizim durur. Sonra eng_scene_load yeniden cagrilabilir (bolum gecisi)."),
    ("eng_scene_loaded", "bool", [], "Su an yuklu bir sahne var mi."),
    ("eng_scene_vx", "float", [("i", "int")], "Sahne varliginin hizi x (govdesizse 0)."),
    ("eng_scene_vy", "float", [("i", "int")], "Sahne varliginin hizi y."),
    ("eng_scene_vz", "float", [("i", "int")], "Sahne varliginin hizi z."),
    ("eng_scene_is_dynamic", "bool", [("i", "int")], "Sahne varliginin govdesi dinamik mi (kuvvet uygulanabilir mi)."),
    ("eng_scene_set_velocity", "void", [("i", "int"), ("vx", "num"), ("vy", "num"), ("vz", "num")], "Sahne govdesinin hizi (dinamik degilse hata loglanir, cagri yok sayilir)."),
    ("eng_scene_impulse", "void", [("i", "int"), ("ix", "num"), ("iy", "num"), ("iz", "num")], "Sahne govdesinin hizina ekler (dinamik degilse hata loglanir)."),
    # varliklar
    ("eng_spawn_box", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("hx", "num"), ("hy", "num"), ("hz", "num"), ("dynamic", "flag"), ("color", "color")], "Kutu (yarim kenarlar) + fizik govdesi. Donus: varlik id (0 hata)."),
    ("eng_spawn_sphere", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("radius", "num"), ("dynamic", "flag"), ("color", "color")], "Kure + fizik govdesi. Donus: varlik id."),
    ("eng_spawn_ground", "int", [("half_size", "num"), ("color", "color")], "Dama dokulu zemin (ust yuz y=0) + sabit govde. Donus: varlik id."),
    ("eng_load_model", "int", [("path", "str")], "glTF modeli yukler. Donus: model tutamaci, -1 hata."),
    ("eng_spawn_model", "int", [("asset", "int"), ("x", "num"), ("y", "num"), ("z", "num"), ("scale", "num"), ("tint", "color")], "Model varligi (govdesiz). Donus: varlik id."),
    ("eng_spawn_light", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("color", "color"), ("intensity", "num"), ("radius", "num")], "Nokta isik. Donus: varlik id."),
    ("eng_despawn", "void", [("id", "int")], "Varligi (ve govdesini) siler."),
    ("eng_alive", "bool", [("id", "int")], "Id hala canli mi (nesil dahil)."),
    ("eng_count", "int", [], "Canli varlik sayisi."),
    ("eng_x", "float", [("id", "int")], "Varlik x (dinamikse sim'den)."),
    ("eng_y", "float", [("id", "int")], "Varlik y."),
    ("eng_z", "float", [("id", "int")], "Varlik z."),
    ("eng_set_pos", "void", [("id", "int"), ("x", "num"), ("y", "num"), ("z", "num")], "Isinlar; dinamik govde yeniden kurulur (hiz sifir)."),
    ("eng_set_color", "void", [("id", "int"), ("color", "color")], "Varlik rengi."),
    ("eng_set_scale", "void", [("id", "int"), ("scale", "num")], "Gorsel olcek (fizik govdesi degismez)."),
    ("eng_set_yaw", "void", [("id", "int"), ("yaw_deg", "num")], "Y ekseni donusu (derece); dinamik govdede sim ezer."),
    ("eng_vx", "float", [("id", "int")], "Hiz x."),
    ("eng_vy", "float", [("id", "int")], "Hiz y."),
    ("eng_vz", "float", [("id", "int")], "Hiz z."),
    ("eng_set_velocity", "void", [("id", "int"), ("vx", "num"), ("vy", "num"), ("vz", "num")], "Dinamik govdenin hizi."),
    ("eng_impulse", "void", [("id", "int"), ("ix", "num"), ("iy", "num"), ("iz", "num")], "Hiza ekler (ziplama, itme)."),
    ("eng_is_dynamic", "bool", [("id", "int")], "Dinamik govde mi."),
    ("eng_awake", "bool", [("id", "int")], "Govde uyanik mi (hareket ediyor)."),
    # model animasyonu
    ("eng_model_clip_count", "int", [("asset", "int")], "Modelin animasyon klip sayisi (glTF animations)."),
    ("eng_model_clip_duration", "float", [("asset", "int"), ("clip", "int")], "Klibin suresi (s)."),
    ("eng_model_clip_name", "str", [("asset", "int"), ("clip", "int")], "Klibin adi."),
    ("eng_set_anim", "void", [("id", "int"), ("clip", "int"), ("speed", "num"), ("loop", "flag")], "Model varligina klip atar; her kare poz degerlendirilir. clip < 0: animasyonu kapat (statik)."),
    ("eng_anim_time", "float", [("id", "int")], "Varligin klip icindeki ani (s)."),
    ("eng_anim_done", "bool", [("id", "int")], "Dongusuz klip bitti mi."),
    # girdi
    ("eng_key_down", "bool", [("name", "str")], "Tus basili mi: \"W\",\"A\",\"SPACE\",\"UP\",\"ESC\",\"ENTER\",\"SHIFT\",\"0\"..\"9\"."),
    ("eng_key_pressed", "bool", [("name", "str")], "Tus bu karede basildi mi."),
    ("eng_touch_count", "int", [], "Ekrandaki parmak sayisi (masaustunde fare = parmak 0)."),
    ("eng_touch_x", "float", [("i", "int")], "i. parmagin x'i (piksel)."),
    ("eng_touch_y", "float", [("i", "int")], "i. parmagin y'si (piksel)."),
    ("eng_stick_x", "float", [], "Sanal joystick x (-1..1; sol yarim ekran surukleme)."),
    ("eng_stick_y", "float", [], "Sanal joystick y (-1..1; ileri = +)."),
    ("eng_stick_action", "bool", [], "Sag yarim ekrana kisa dokunus (bu karede)."),
    ("eng_look_dx", "float", [], "Sag yarim surukleme, piksel/kare (bakis)."),
    ("eng_mouse_x", "float", [], "Fare x (masaustu)."),
    ("eng_mouse_y", "float", [], "Fare y (masaustu)."),
    ("eng_mouse_down", "bool", [("button", "int")], "Fare tusu basili mi (0 sol, 1 sag, 2 orta)."),
    # HUD
    ("eng_text", "void", [("s", "str"), ("x", "num"), ("y", "num"), ("scale", "num"), ("color", "color")], "Ekrana metin (kare icinde cagir; font yoksa sessizce atlanir, logda uyari)."),
    ("eng_rect", "void", [("x", "num"), ("y", "num"), ("w", "num"), ("h", "num"), ("color", "color")], "Ekrana duz dikdortgen (kare icinde cagir)."),
    ("eng_text_width", "float", [("s", "str"), ("scale", "num")], "Metnin piksel genisligi."),
    # ses
    ("eng_audio_open", "bool", [("rate", "int"), ("channels", "int")], "Ses cihazini acar (0,0 = varsayilan 48 kHz stereo). Acilamazsa false: oyun sessiz devam eder, hata loglanir."),
    ("eng_audio_close", "void", [], "Ses cihazini kapatir (calan sesler durur)."),
    ("eng_audio_ok", "bool", [], "Ses cihazi acik mi."),
    ("eng_audio_backend", "str", [], "Ses arka ucu ve cihaz adi (\"pulseaudio 'Speakers' 48000 Hz ...\"); kapaliysa bos."),
    ("eng_audio_load", "int", [("path", "str")], "Ses dosyasi yukler (WAV/FLAC/MP3) -> klip tutamaci, -1 hata. Ayni yol tek kez yuklenir."),
    ("eng_audio_tone", "int", [("hz", "num"), ("seconds", "num")], "Sentetik sinus klibi uretir -> klip tutamaci. Ayni (frekans, sure) ayni tutamaci verir."),
    ("eng_audio_play", "int", [("clip", "int"), ("gain", "num"), ("loop", "flag")], "Klibi calar. Donus: ses id'si (0 hata). Ses seviyesi ana seviyeyle carpilir."),
    ("eng_audio_beep", "int", [("hz", "num"), ("seconds", "num"), ("gain", "num")], "Ton uret + cal (tek cagri). Donus: ses id'si."),
    ("eng_audio_stop", "void", [("voice", "int")], "Calan sesi durdurur."),
    ("eng_audio_stop_all", "void", [], "Butun sesleri durdurur."),
    ("eng_audio_master", "void", [("gain", "num")], "Ana ses seviyesi (0..4); calan sesler de guncellenir."),
    ("eng_audio_playing", "int", [], "Su an calan ses sayisi (cihaz thread'inin olctugu deger)."),
    ("eng_audio_peak", "float", [], "Son ses blogundaki en buyuk ornek (0 = sessiz); ses gercekten uretiliyor mu olcusu."),
    # sorgular: isin testi + yakinlik (savas/yapay zeka; callback FFI olmadigi icin OLAY degil SORGU)
    ("eng_raycast", "float", [("ox", "num"), ("oy", "num"), ("oz", "num"), ("dx", "num"), ("dy", "num"), ("dz", "num"), ("max_dist", "num"), ("skip_id", "int")], "Isin testi: (ox,oy,oz)'dan (dx,dy,dz) yonune en cok max_dist. Donus: carpma mesafesi, -1 iska. skip_id kendi govdesini atlar (0 = atlama yok). Govde eklendikten sonra en az bir kare gerekir (genis faz)."),
    ("eng_ray_x", "float", [], "Son isin testinin carpma noktasi x."),
    ("eng_ray_y", "float", [], "Son isin testinin carpma noktasi y."),
    ("eng_ray_z", "float", [], "Son isin testinin carpma noktasi z."),
    ("eng_ray_nx", "float", [], "Son carpmanin yuzey normali x (duvar boyunca kayma)."),
    ("eng_ray_ny", "float", [], "Son carpmanin yuzey normali y."),
    ("eng_ray_nz", "float", [], "Son carpmanin yuzey normali z."),
    ("eng_ray_id", "int", [], "Son isinin carptigi KOPRU varliginin id'si (0 = kopru varligi degil)."),
    ("eng_ray_scene", "int", [], "Son isinin carptigi SAHNE varliginin dizini (-1 = sahne govdesi degil)."),
    ("eng_overlap", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("radius", "num"), ("skip_id", "int")], "Kure sorgusu: merkeze radius icindeki kopru varliklari, YAKINDAN UZAGA sirali. Donus: sonuc sayisi (eng_overlap_id/eng_overlap_dist ile okunur). Zemin ve isik disarida."),
    ("eng_overlap_id", "int", [("i", "int")], "Son kure sorgusunun i. sonucunun varlik id'si."),
    ("eng_overlap_dist", "float", [("i", "int")], "Son kure sorgusunun i. sonucunun merkeze uzakligi."),
    ("eng_nearest", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("radius", "num"), ("skip_id", "int")], "Merkeze radius icindeki EN YAKIN kopru varligi (0 = yok). eng_overlap ile ayni olcut."),
    # --- Carpisma olaylari: callback FFI yok, o yuzden KUYRUK -------------
    ("eng_collision_count", "int", [], "Bu karede olusan carpisma olayi sayisi. Halka her kare basinda temizlenir; bir karede birden cok fizik adimi atilsa da olaylar birikir."),
    ("eng_collision_dropped", "int", [], "Halkaya sigmayip DUSEN olay sayisi. 0 degilse kapasite yetmiyor: olaylar sessizce kirpilmiyor, bu sayac goruyor."),
    ("eng_collision_a", "int", [("i", "int")], "i. carpismanin A tarafi: kopru varlik id'si (0 = kopru varligi degil, ornegin zemin ya da sahne govdesi)."),
    ("eng_collision_b", "int", [("i", "int")], "i. carpismanin B tarafi."),
    ("eng_collision_scene_a", "int", [("i", "int")], "i. carpismanin A tarafinin SAHNE varlik dizini (-1 = sahne govdesi degil)."),
    ("eng_collision_scene_b", "int", [("i", "int")], "i. carpismanin B tarafinin sahne varlik dizini."),
    ("eng_collision_x", "float", [("i", "int")], "i. carpismanin temas noktasi (dunya x)."),
    ("eng_collision_y", "float", [("i", "int")], "i. carpismanin temas noktasi (dunya y)."),
    ("eng_collision_z", "float", [("i", "int")], "i. carpismanin temas noktasi (dunya z)."),
    ("eng_collision_nx", "float", [("i", "int")], "i. carpismanin yuzey normali (A'dan B'ye) x."),
    ("eng_collision_ny", "float", [("i", "int")], "i. carpismanin yuzey normali y."),
    ("eng_collision_nz", "float", [("i", "int")], "i. carpismanin yuzey normali z."),
    ("eng_collision_speed", "float", [("i", "int")], "i. carpismanin SIDDETI: temas noktasinda normal boyu goreli hiz (m/s). Sert/yumusak carpma ayrimi bununla yapilir."),
    # navmesh: bake engine_sahnec'te (sahnedeki sabit kutu govdeler), runtime yalniz sorgular
    ("eng_nav_ok", "bool", [], "Yuklu sahnede bake edilmis navmesh var mi (yoksa oyun duz yola duser)."),
    ("eng_nav_polys", "int", [], "Navmesh poligon sayisi."),
    ("eng_nav_path", "int", [("fx", "num"), ("fy", "num"), ("fz", "num"), ("tx", "num"), ("ty", "num"), ("tz", "num")], "Navmesh uzerinde duz yol: nokta sayisi (0 = yol yok). Noktalar eng_nav_x/y/z ile okunur."),
    ("eng_nav_partial", "bool", [], "Son yol kismi mi (hedefe ulasilamadi, en yakin noktaya kadar)."),
    ("eng_nav_x", "float", [("i", "int")], "Son yolun i. noktasinin x'i."),
    ("eng_nav_y", "float", [("i", "int")], "Son yolun i. noktasinin y'si."),
    ("eng_nav_z", "float", [("i", "int")], "Son yolun i. noktasinin z'si."),
    ("eng_nav_nearest", "bool", [("x", "num"), ("y", "num"), ("z", "num")], "Navmesh'e en yakin noktayi bulur (konum mesh'in disindaysa yol aramadan once buna yapistir). Sonuc eng_nav_near_x/y/z."),
    ("eng_nav_near_x", "float", [], "Son eng_nav_nearest sonucunun x'i."),
    ("eng_nav_near_y", "float", [], "Son eng_nav_nearest sonucunun y'si."),
    ("eng_nav_near_z", "float", [], "Son eng_nav_nearest sonucunun z'si."),
    ("eng_nav_raycast", "bool", [("fx", "num"), ("fy", "num"), ("fz", "num"), ("tx", "num"), ("ty", "num"), ("tz", "num")], "Navmesh uzerinde dogru gorus: true = ENGEL VAR (yol ara), false = dumduz gidilebilir. Yol aramadan cok daha ucuz."),
    ("eng_nav_ray_t", "float", [], "Son eng_nav_raycast'in carpma parametresi (0..1; engel yoksa 1)."),
    # anlik-kip (immediate mode) arayuz: menu / ayar / duraklat. Motorda yeni cizim
    # yolu yok: eng_rect + eng_text uzerine kurulu. Deger BETIKTE yasar (Tulpar'da
    # cikti parametresi yok): onay kutusu/kaydirici YENI degeri dondurur.
    ("eng_ui_begin", "void", [], "Arayuz karesini baslatir: isaretciyi (enjeksiyon > dokunmatik > fare) ornekler, basma/birakma kenarlarini hesaplar. Kare icinde, widget'lardan ONCE."),
    ("eng_ui_end", "void", [], "Arayuz karesini bitirir: isaretci kalktiysa surukleme birakilir."),
    ("eng_ui_theme", "void", [("panel", "color"), ("text", "color"), ("accent", "color")], "Arayuz renkleri: panel zemini, yazi, vurgu. Dugme/iz tonlari panelden turetilir."),
    ("eng_ui_enable", "void", [("on", "flag")], "Bundan sonraki widget'lar etkin mi. Kapaliyken widget SOLUK cizilir ve degeri DEGISMEZ (tiklama yutulur)."),
    ("eng_ui_panel", "void", [("x", "num"), ("y", "num"), ("w", "num"), ("h", "num"), ("color", "color")], "Menu/HUD zemini (renk 0 = tema paneli). eng_ui_begin gerektirmez."),
    ("eng_ui_label", "void", [("s", "str"), ("x", "num"), ("y", "num"), ("scale", "num"), ("color", "color")], "Arayuz metni (renk 0 = tema yazi rengi). eng_ui_begin gerektirmez."),
    ("eng_ui_button", "bool", [("label", "str"), ("x", "num"), ("y", "num"), ("w", "num"), ("h", "num")], "Dugme: bu karede tiklandiysa true (basma VE birakma dugmenin icinde). Kimlik etiketten turer."),
    ("eng_ui_checkbox", "bool", [("label", "str"), ("x", "num"), ("y", "num"), ("w", "num"), ("h", "num"), ("value", "flag")], "Onay kutusu. Donus YENI degerdir (degisen yoksa gelen deger) — betik geri yazar."),
    ("eng_ui_slider", "float", [("label", "str"), ("x", "num"), ("y", "num"), ("w", "num"), ("h", "num"), ("value", "num"), ("min_v", "num"), ("max_v", "num")], "Kaydirici. Donus YENI degerdir (surukleme sirasinda her kare gunceller); aralik gecersizse hata + gelen deger."),
    ("eng_ui_active", "bool", [], "Bir widget basili/surukleniyor mu (menu acikken oyun girdisini bastirmak icin)."),
    ("eng_ui_clicks", "int", [], "Toplam etkinlestirme sayisi: dugme tiklamasi + onay kutusu degisimi (kapi olcumu)."),
    ("eng_ui_test_click", "void", [("x", "num"), ("y", "num")], "Pencersiz dogrulama: (x,y) noktasina tek karelik bas+birak enjekte eder; sonraki eng_ui_begin tuketir. Widget disina tiklama tetiklemez."),
    # kalici kayit: ayarlar + en yuksek skor (motor kurulmadan da calisir)
    ("eng_save_open", "bool", [("path", "str")], "Kayit deposunu bu dosyaya baglar ve okur. true = dosya okundu, false = dosya yok (yeni kayit; hata degil). Varsayilan yol: TULPAR_ENGINE_SAVE ya da calisma dizininde tulpar_kayit.txt."),
    ("eng_save_path", "str", [], "Kayit dosyasinin yolu."),
    ("eng_save_set", "void", [("key", "str"), ("value", "num")], "Sayisal deger yazar (bellekte; diske eng_save_write ya da kapanis yazar)."),
    ("eng_save_get", "float", [("key", "str"), ("def", "num")], "Sayisal deger okur. Anahtar yoksa varsayilan doner (hata degil); deger sayisal degilse HATA loglanir ve varsayilan doner."),
    ("eng_save_set_str", "void", [("key", "str"), ("value", "str")], "Metin deger yazar (satir sonu ve '=' yasak)."),
    ("eng_save_get_str", "str", [("key", "str"), ("def", "str")], "Metin deger okur; anahtar yoksa varsayilan."),
    ("eng_save_has", "bool", [("key", "str")], "Anahtar var mi."),
    ("eng_save_write", "bool", [], "Kaydi diske yazar (gecici dosya + rename). Kapanista kirli kayit kendiliginden yazilir."),
    ("eng_save_clear", "void", [], "Butun anahtarlari dusurur (diske yazmak icin eng_save_write)."),
    ("eng_save_count", "int", [], "Kayittaki anahtar sayisi."),
    # sahne sicak yeniden yukleme: editorde "Derle" -> oyun kendini yeniler
    ("eng_scene_watch", "void", [("on", "flag")], "Yuklu .sahneb dosyasini izle: degisirse sahne bosaltilip yeniden yuklenir. Degisim gorulunce bir kontrol daha beklenir (yarim yazilmis blob yuklenmesin)."),
    ("eng_scene_reloaded", "bool", [], "Son okumadan beri sahne sicak yuklendi mi. BIR KEZ true doner (tuketilir): olay olarak kullanilir."),
    ("eng_scene_reload_count", "int", [], "Toplam sicak yukleme sayisi."),
    ("eng_scene_path", "str", [], "Yuklu (ya da izlenen) sahne dosyasinin yolu."),
    ("eng_file_copy", "bool", [("src", "str"), ("dst", "str")], "Ikili dosya kopyalar. .sahneb gibi NUL iceren bloblar Tulpar string'ine sigmaz; arac/test betikleri icin."),
    ("eng_file_mtime", "float", [("path", "str")], "Dosyanin degisim zamani (saniye, epoch); dosya yoksa 0."),
    # olcum
    ("eng_draw_count", "int", [], "Son karede cizim sayisi."),
    ("eng_body_count", "int", [], "Fizikteki govde sayisi."),
    ("eng_light_count", "int", [], "Son karede nokta isik sayisi."),
    ("eng_frame_ms", "float", [], "Son 120 karenin p50 suresi (ms)."),
]

C_PARAM = {"num": "double", "int": "int", "color": "int64_t", "str": "const char *", "flag": "int"}
UNPACK = {"num": "tm_num({v})", "int": "(int)tm_int({v})", "color": "tm_int({v})", "str": "tm_str({v})", "flag": "(int)tm_int({v})"}
TI_PARAM = {"num": "TYPE_UNKNOWN", "int": "TYPE_INT", "color": "TYPE_INT", "str": "TYPE_STRING", "flag": "TYPE_UNKNOWN"}
TI_RET = {"void": "TYPE_VOID", "bool": "TYPE_BOOL", "int": "TYPE_INT", "float": "TYPE_FLOAT", "str": "TYPE_STRING"}
LSP_T = {"num": "num", "int": "int", "color": "int", "str": "str", "flag": "bool", "void": "void", "bool": "bool", "float": "float"}
MAX_ARGS = 8


def gen_bindings(root):
    out = []
    out.append("// URETILMIS DOSYA — engine/tools/gen_engine_bindings.py (SPEC tablosu). Elle duzenleme.")
    out.append("//")
    out.append("// Tulpar Engine kopru bindingleri: `import \"engine\"` eden programin cagirdigi")
    out.append("// aot_eng_*_ptr builtinleri (N-pointer VMValue ABI'si, aot_tm_* ile ayni) ->")
    out.append("// engine/bridge/engine_api.h'deki duz skaler teng_* C API'si. libtulpar_engine.a")
    out.append("// icinde yasar; link satirina yalniz backend->uses_engine isaretliyken girer.")
    out.append('#include "../src/vm/vm.hpp"')
    out.append('#include "../engine/bridge/engine_api.h"')
    out.append("#include <cstdint>")
    out.append("#include <cstring>")
    out.append("")
    out.append("extern \"C\" ObjString *vm_alloc_string_aot(void *vm, const char *chars, int length);")
    out.append("")
    out.append("namespace {")
    out.append("double tm_num(const VMValue *v) {")
    out.append("  if (!v) return 0.0;")
    out.append("  if (IS_INT(*v)) return (double)AS_INT(*v);")
    out.append("  if (IS_FLOAT(*v)) return AS_FLOAT(*v);")
    out.append("  if (IS_BOOL(*v)) return AS_BOOL(*v) ? 1.0 : 0.0;")
    out.append("  return 0.0;")
    out.append("}")
    out.append("int64_t tm_int(const VMValue *v) {")
    out.append("  if (!v) return 0;")
    out.append("  if (IS_INT(*v)) return AS_INT(*v);")
    out.append("  if (IS_FLOAT(*v)) return (int64_t)AS_FLOAT(*v);")
    out.append("  if (IS_BOOL(*v)) return AS_BOOL(*v) ? 1 : 0;")
    out.append("  return 0;")
    out.append("}")
    out.append("const char *tm_str(const VMValue *v) { return v && IS_STRING(*v) ? AS_STRING(*v)->chars : \"\"; }")
    out.append("VMValue tm_make_str(const char *s) {")
    out.append("  if (!s) s = \"\";")
    out.append("  ObjString *o = vm_alloc_string_aot(nullptr, s, (int)strlen(s));")
    out.append("  return VM_OBJ((Obj *)o);")
    out.append("}")
    out.append("} // namespace")
    out.append("")
    out.append("extern \"C\" {")
    for name, ret, params, _doc in SPEC:
        assert len(params) <= MAX_ARGS, name
        sig = ", ".join(f"VMValue *{p}" for p, _ in params) or "void"
        call = ", ".join(UNPACK[t].format(v=p) for p, t in params)
        out.append(f"VMValue aot_{name}_ptr({sig}) {{")
        expr = f"t{name}({call})"
        if ret == "void":
            out.append(f"  {expr};")
            out.append("  return VM_VOID();")
        elif ret == "bool":
            out.append(f"  return VM_BOOL({expr} != 0);")
        elif ret == "int":
            out.append(f"  return VM_INT((int64_t){expr});")
        elif ret == "float":
            out.append(f"  return VM_FLOAT({expr});")
        elif ret == "str":
            out.append(f"  return tm_make_str({expr});")
        out.append("}")
    out.append("} // extern \"C\"")
    out.append("")
    write(os.path.join(root, "runtime", "engine_bindings.cpp"), "\n".join(out))


def gen_table(root):
    out = ["// URETILMIS DOSYA — engine/tools/gen_engine_bindings.py. {ad, sembol, arite} (TameBuiltin yerlesimi)."]
    for name, _ret, params, _doc in SPEC:
        out.append(f'    {{"{name}", "aot_{name}_ptr", {len(params)}}},')
    write(os.path.join(root, "src", "aot", "engine_builtins_table.inc"), "\n".join(out) + "\n")


def gen_typeinfer(root):
    out = ["      // URETILMIS — engine/tools/gen_engine_bindings.py: eng_* (Tulpar Engine koprusu) imzalari."]
    for name, ret, params, _doc in SPEC:
        ps = ", ".join(TI_PARAM[t] for _, t in params)
        out.append(f'      {{"{name}", {TI_RET[ret]}, {{{ps}}}}},')
    write(os.path.join(root, "src", "typeinfer", "engine_builtins_sigs.inc"), "\n".join(out) + "\n")


def gen_lsp(root):
    out = ["    // URETILMIS — engine/tools/gen_engine_bindings.py: eng_* (Tulpar Engine koprusu)."]
    for name, ret, params, doc in SPEC:
        ps = ", ".join(f"{p}: {LSP_T[t]}" for p, t in params)
        sig = f"{name}({ps})" + (f": {LSP_T[ret]}" if ret != "void" else "")
        doc_c = doc.replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'    {{"{name}", "{sig}", "{doc_c}"}},')
    write(os.path.join(root, "src", "lsp", "engine_builtins.inc"), "\n".join(out) + "\n")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    print("yazildi:", os.path.relpath(path), f"({len(SPEC)} builtin)")


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    names = [s[0] for s in SPEC]
    assert len(names) == len(set(names)), "yinelenen ad"
    gen_bindings(root)
    gen_table(root)
    gen_typeinfer(root)
    gen_lsp(root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
