// L6 CONTENT — Yazilim tabanli occlusion culling (500 madde listesi #93
// "Software Occlusion"). Kucuk cozunurluklu bir CPU derinlik tamponuna
// (ör. 256x256 -- gercek GPU derinlik hedefinden COK daha kucuk, hizli) az
// sayida BUYUK occluder (bina/duvar/zemin gibi) rasterize edilir; aday
// nesneler bu tampona karsi test edilerek TAMAMEN gizliyse cizim listesine
// hic girmez. renderer/render_size'daki GERCEK derinlik tamponundan BAGIMSIZ,
// saf 2B/skaler bir algoritma -- ekran-uzayi projeksiyonu (Mat4/Frustum
// zaten motorda var) cagiranin sorumlulugu, bu dosya YALNIZ "verilen
// dikdortgen+derinlik occluded mi" sorusuna cevap verir.
//
// content/ katmani renderer/'a BAGIMLI DEGIL (PLAN.md katman kurali).
#pragma once
#include <cstdint>

namespace tulpar::engine::content {

struct DepthBuffer {
  float *cells = nullptr; // cagiranin ayirdigi width*height dizi (satir-major)
  uint32_t width = 0, height = 0;
};

// TUM hucreleri `far_value`e sifirlar (kare basina bir kez, occluder'lar
// rasterize edilmeden ONCE cagirilir).
void clear_depth_buffer(DepthBuffer &db, float far_value);

// Ekran-uzayi (hucre koordinatlarinda) [min_x,max_x)x[min_y,max_y) dikdortgenini
// `depth` ile rasterize eder: HER hucrede, `depth` ORADAKI mevcut degerden
// KUCUKSE (yani bu occluder DAHA YAKINSA -- standart z-buffer "yakin kazanir"
// kurali) hucre guncellenir. Tampon disina TASAN kisimlar sessizce kirpilir.
void rasterize_occluder_rect(DepthBuffer &db, float min_x, float min_y, float max_x, float max_y, float depth);

// Aday nesnenin ekran-uzayi dikdortgeni + EN YAKIN (min) derinligi verilir.
// KAPLADIGI HER hucrede occluder KESINLIKLE daha yakinsa (depth buffer degeri
// < depth) nesne o hucrede gizlidir; TUM hucrelerde gizliyse true (TAMAMEN
// occluded) doner. Herhangi bir hucrede occluder yoksa/daha uzaksa VEYA
// dikdortgen tamponun tamamen DISINDAysa false (guvenli varsayim: gorunur
// sayilir, YANLIS ELEME yerine YANLIS CIZIM tercih edilir).
bool is_occluded(const DepthBuffer &db, float min_x, float min_y, float max_x, float max_y, float depth);

} // namespace tulpar::engine::content
