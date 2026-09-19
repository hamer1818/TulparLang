// L6 CONTENT — prefab: sahne alt agacini dosyaya al, baska sahneye ornekle.
//
// NEDEN: Unity Prefab, UE5 Blueprint Class, Roblox Model, Source 2 Prefab --
// bes motorun hepsinde bir "yeniden kullanim birimi" var. Bizde yoktu: 50
// agaclik bir orman 50 elle kurulmus varlik demekti.
//
// BICIM: prefab dosyasi SIRADAN bir .sahne metnidir (scene_save/scene_load).
// Ayri bir bicim icat edilmedi: yazici/ayristirici zaten kapili (scene_check),
// kanonik ve deterministik. Prefab = kokun yerel orijine tasindigi, yalniz
// alt agacin ve kullandigi kaynaklarin bulundugu bir sahne.
//
// INDEKS ALANLARI: SceneEntity uc alanda BASKA bir seye indeks tutar ve
// uc'u de yeniden eslenir -- yoksa ornek, hedef sahnede YANLIS varliga/kaynaga
// isaret eder:
//   parent        -> varlik indeksi
//   joint_target  -> varlik indeksi (alt agac DISINA isaret ediyorsa -1)
//   asset         -> kaynak indeksi (yol ile eslenir; yoksa eklenir)
#pragma once
#include <cstdint>

#include "content/scene.hpp"

namespace tulpar::engine::content {

// `root` ve tum torunlarini `out`a cikarir. Sira on-siradir (ebeveyn cocuktan
// ONCE), kok yerel orijine tasinir (donus/olcek korunur), yalniz kullanilan
// kaynaklar tasinir. out'un dunya ayarlari varsayilana doner.
// Donus: cikarilan varlik sayisi; 0 = gecersiz kok.
uint32_t prefab_extract(const SceneDesc &src, int32_t root, SceneDesc *out);

// `prefab`i `dst`nin SONUNA ekler; varliklar h uzerinden (geri alinabilir),
// kaynaklar dst.add_asset ile (yol ayniysa mevcut indeks). Kok(ler) `at`
// konumuna tasinir.
//
// HEPSI YA DA HICBIRI: varlik ya da kaynak tablosu yetmeyecekse, ya da prefab
// bir cocugu ebeveyninden once listeliyorsa (elle bozulmus dosya) HICBIR sey
// eklenmez ve 0 doner. Yari eklenmis bir prefab, kopuk ebeveyn indeksleriyle
// sahneyi bozardi.
//
// Donus: eklenen varlik sayisi. first_index verildiyse ilk eklenenin indeksi.
uint32_t prefab_instantiate(SceneDesc &dst, SceneHistory &h, const SceneDesc &prefab, Vec3 at, uint32_t *first_index);

} // namespace tulpar::engine::content
