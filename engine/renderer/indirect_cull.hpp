// L3 RENDERER — Instance frustum culling + mesafeye-gore LOD secimi,
// Vulkan'in VkDrawIndexedIndirectCommand duzeniyle BIREBIR eslesen bir
// tampon uretir.
//
// **SIFIRDAN TASARIM DEGIL, GERCEK/KANITLANMIS bir teknigin PORTU:** Sascha
// Willems'in (MIT lisansli, github.com/SaschaWillems/Vulkan) "Compute shader
// culling and LOD" ornegi (examples/computecullandlod/cull.comp) -- Vulkan
// ogrenmek isteyen SAYISIZ gercek motorun/gelistiricinin referans aldigi,
// yaygin bilinen bir teknik. Bu dosya o compute shader'in frustum-testi +
// mesafe-esikli-LOD-secimi MANTIGINI BIREBIR izler; fark yalniz YURUTME
// YERI: bu makinede glslc/Vulkan SDK YOK, bir compute shader yazip TEK BIR
// satirini bile calistirip dogrulamak IMKANSIZ -- ayni algoritma, CPU'da,
// tam hand-trace ile kanitlanabilir bir bicimde portlandi (renderer/cluster.hpp
// da AYNI gerekceyle "neden GPU degil CPU" karari verir).
//
// **ONARILAN bir sadelestirme:** orijinal ornek frustum testinde SABIT
// yaricap (1.0) kullanir (demo basitligi icin, InstanceData.scale hic
// kullanilmaz). Burada dogru mesafe-olcekli kirpma icin PER-INSTANCE
// bounding_radius kullanilir -- aksi halde buyuk nesneler kamera yakininda
// bile erken/yanlis kirpilir.
//
// Kapsam: tek indirect-komut-dizisi (instance basina bir slot, orijinal
// ornekle AYNI) -- mesh turune gore GRUPLAMA/sikistirma SONRAKI adim.
// vertex_offset her zaman 0: content/model.hpp'nin LOD tasarimiyla AYNI
// varsayim (tum LOD'lar TEK vertex tamponunu, farkli indeks alt-kumeleriyle
// paylasir).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

// Alan sirasi VkDrawIndexedIndirectCommand ile BIREBIR ayni -- gercek
// vkCmdDrawIndexedIndirect bu yapiyi DOGRUDAN (donusumsuz) kullanabilir.
struct IndexedIndirectCommand {
  uint32_t index_count = 0;
  uint32_t instance_count = 0; // 0 = bu kare CIZILMEZ (kirpildi)
  uint32_t first_index = 0;
  int32_t vertex_offset = 0;
  uint32_t first_instance = 0;
};

struct CullInstance {
  Vec3 pos{0, 0, 0};
  float bounding_radius = 1.0f;
};

// lods[]: MESAFEYE GORE ARTAN sirada verilmelidir (cagiran garanti eder).
// Son eleman "varsayilan/en kaba" LOD'dur -- distance alani KULLANILMAZ
// (hicbir zaman kontrol edilmez, tam da cull.comp'taki MAX_LOD_LEVEL
// davranisi: ilk (lod_count-1) esigi de gecemeyen her sey son seviyeye duser).
struct LodLevel {
  uint32_t first_index = 0;
  uint32_t index_count = 0;
  float distance = 0.0f;
};

constexpr uint32_t kMaxCullLodLevels = 8;
struct CullStats {
  uint32_t draw_count = 0; // frustum'da KALAN instance sayisi (kirpilenler HARIC)
  uint32_t lod_count[kMaxCullLodLevels] = {}; // her seviyeden kac instance secildi
};

// instances[count] / out_commands[count]: cagiranin ONCEDEN ayirdigi, AYNI
// boyutta iki dizi (instance i -> out_commands[i], birebir). lod_count==0
// ise false doner (gecersiz cagri, hicbir LOD secilemez).
bool cull_and_select_lod(const Frustum &frustum, Vec3 camera_pos,
                          const CullInstance *instances, uint32_t count,
                          const LodLevel *lods, uint32_t lod_count,
                          IndexedIndirectCommand *out_commands, CullStats *out_stats);

} // namespace tulpar::engine::renderer
