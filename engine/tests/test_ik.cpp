// sim/ik.hpp: analitik iki-kemik IK. Kosinus teoremiyle elle hesaplanmis bir
// "45 derece" durumu (tam sayilarla kanitlanabilir tek ornek) + sinir
// durumlari (erisilemez uzak hedef, erisilemez cok yakin hedef, hedef=kok).
#include <cmath>

#include "core/math/vec.hpp"
#include "sim/ik.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

ENGINE_TEST(ik_two_bone_reaches_exact_45_degree_target) {
  // Elle hesap: len1=len2=1, hedef (0,0,sqrt(2)) -- kosinus teoremi:
  // cos(A) = (1+2-1)/(2*sqrt(2)) = 1/sqrt(2) -> A=45 derece.
  // mid = (sin(45), 0, cos(45))*1 = (0.7071, 0, 0.7071) -- pole (1,0,0)
  // bukulmeyi +X'e dogru zorluyor.
  const float target_z = std::sqrt(2.0f);
  TwoBoneIkResult r = solve_two_bone_ik({0, 0, 0}, {0, 0, target_z}, {1, 0, 0}, 1.0f, 1.0f);
  CHECK(r.reached);
  CHECK(nearly_equal(r.end, Vec3{0, 0, target_z}, 1e-4f)); // hedefe TAM ulasildi
  CHECK(nearly_equal(r.mid, Vec3{0.70710678f, 0, 0.70710678f}, 1e-4f));
  // Kemik uzunluk kisitlari HER ZAMAN tam saglanir (clamp'ten bagimsiz kimlik).
  CHECK(nearly_equal(length(r.mid), 1.0f, 1e-4f));
  CHECK(nearly_equal(length(r.end - r.mid), 1.0f, 1e-4f));
}

ENGINE_TEST(ik_two_bone_unreachable_far_extends_to_max_reach) {
  // Hedef (5,0,0), len1=len2=1 -> max erisim 2. Kol tam gerilir: uc efektor
  // TAM olarak kok+2 birim boyunca hedef yonunde, hedefe ULASAMAZ.
  TwoBoneIkResult r = solve_two_bone_ik({0, 0, 0}, {5, 0, 0}, {0, 1, 0}, 1.0f, 1.0f);
  CHECK(!r.reached);
  CHECK(nearly_equal(r.end, Vec3{2, 0, 0}, 1e-4f)); // max_reach = len1+len2 = 2
  CHECK(nearly_equal(length(r.mid), 1.0f, 1e-4f));  // |mid-root|=len1 HER ZAMAN tam (clamp'ten bagimsiz kimlik)
  // |end-mid|=len2 SADECE reached==true iken tam saglanir (bkz. Test 1); burada
  // acos-sinir epsilon'u (1e-4) yuzunden ~1e-4 mertebesinde bir sapma var --
  // gevsek tolerans kasitli (bu kesin bir kimlik degil, yaklasik).
  CHECK(nearly_equal(length(r.end - r.mid), 1.0f, 1e-3f));
  CHECK(nearly_equal(r.mid, Vec3{1, 0, 0}, 0.02f)); // neredeyse duz (kucuk acos-sinir bukulmesi haric)
}

ENGINE_TEST(ik_two_bone_unreachable_close_clamps_to_min_reach) {
  // Farkli kemik boylari: len1=2, len2=1 -> min erisim |2-1|=1. Hedef (0.3,0,0)
  // bu sinirdan DAHA YAKIN -- kol maksimum katlanir, uc efektor koke min_reach
  // (1 birim) kadar yaklasir, hedefe ULASAMAZ.
  TwoBoneIkResult r = solve_two_bone_ik({0, 0, 0}, {0.3f, 0, 0}, {0, 1, 0}, 2.0f, 1.0f);
  CHECK(!r.reached);
  CHECK(nearly_equal(r.end, Vec3{1, 0, 0}, 1e-4f)); // min_reach = |2-1| = 1
  CHECK(nearly_equal(length(r.mid), 2.0f, 1e-4f));  // len1 kisitla TAM saglanir
  CHECK(nearly_equal(length(r.end - r.mid), 1.0f, 1e-3f)); // yaklasik (bkz. yukaridaki test notu)
}

ENGINE_TEST(ik_two_bone_target_at_root_uses_pole_as_fallback_axis) {
  // Hedef TAM kokte (yon tanimsiz) -- esit kemiklerde (min_reach=0) bu
  // ASLINDA erisilebilir (kol tam katlanip koke geri doner), ve yon
  // BELIRSIZLIGI pole ile cozulmeli (coksmemeli/NaN uretmemeli).
  TwoBoneIkResult r = solve_two_bone_ik({0, 0, 0}, {0, 0, 0}, {1, 0, 0}, 1.0f, 1.0f);
  CHECK(r.reached);
  CHECK(nearly_equal(r.end, Vec3{0, 0, 0}, 1e-4f));
  CHECK(nearly_equal(length(r.mid), 1.0f, 1e-3f)); // NaN degil, len1 kadar uzakta
}
