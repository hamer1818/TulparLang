#include "renderer/cluster.hpp"

#include <cmath>
#include <cstring>

namespace tulpar::engine::renderer {

void cluster_slice_params(const ClusterGrid &g, float *scale, float *bias) {
  const float lr = std::log(g.zfar / g.znear);
  *scale = (float)g.z / lr;
  *bias = -(float)g.z * std::log(g.znear) / lr;
}

uint32_t cluster_slice_of(const ClusterGrid &g, float d) {
  float scale, bias;
  cluster_slice_params(g, &scale, &bias);
  float s = std::floor(std::log(d > 1e-6f ? d : 1e-6f) * scale + bias);
  if (s < 0) s = 0;
  if (s > (float)(g.z - 1)) s = (float)(g.z - 1);
  return (uint32_t)s;
}

namespace {
// Isigin KUME UZAYINDAKI ayak izi: [tx0,tx1] x [ty0,ty1] x [s0,s1]. Onem
// olcusu bundan turer, yani projeksiyon tersi/centroid hesabi GEREKMEZ —
// on-dondurmeli (Android) ve ortografik projeksiyonda da aynen calisir.
struct LightBox {
  int tx0, tx1, ty0, ty1;
  uint32_t s0, s1;
  float intensity, radius;
};
// Kume (x,y,z) icin isigin onemi = O KUMEDEKI BEKLENEN KATKI (goreli).
//
// Ayak izi kutusu, yaricapi r olan kurenin izdusumudur: her eksende yari-genislik
// tam olarak r'ye karsilik gelir. Yani kutu merkezinden normalize uzaklik rho,
// dunya uzayinda d = rho * r demektir.
//
// NEDEN YALNIZ TERS-KARE, SONUM PENCERESI DEGIL: shader'in sonumu
// pencere^2/(d^2+1), pencere = 1 - (d^2/r^2)^2 — yani d'nin SEKIZINCI kuvveti
// mertebesinde bir terim. Buradaki d ise kume duzeyinde KABA bir tahmin
// (kumenin merkezi, gercekte golgelenen yuzey degil). Kaba bir d'yi sekizinci
// kuvvete sokmak hatayi devasa buyutur: kutunun kenarina yakin isiklarin onemi
// sifira gider, oran tahmin edicisi onlarin enerjisini "yok" sayar ve kare
// SISTEMATIK kararir. OLCULDU (ayni sahne, 24 karelik ortalama):
//     pencere^2/(d^2+1) -> %19.3 karartma
//     pencere/(d^2+1)   -> %13.6
//     1/(d^2+1)         -> %1.95   <-- secilen
// Ters-kare terimi d'deki hataya karsi dayanikli ve siralamayi (yakin = onemli)
// dogru kuruyor. <nl> kosinus carpani da yok: kume icinde ortalamasi ~sabit,
// oranda sadelesir.
float box_importance(const LightBox &b, uint32_t x, uint32_t y, uint32_t z) {
  const float cx = 0.5f * (float)(b.tx0 + b.tx1), hx = 0.5f * (float)(b.tx1 - b.tx0) + 0.5f;
  const float cy = 0.5f * (float)(b.ty0 + b.ty1), hy = 0.5f * (float)(b.ty1 - b.ty0) + 0.5f;
  const float cz = 0.5f * (float)(b.s0 + b.s1), hz = 0.5f * (float)(b.s1 - b.s0) + 0.5f;
  const float dx = ((float)x - cx) / hx, dy = ((float)y - cy) / hy, dz = ((float)z - cz) / hz;
  float rho2 = dx * dx + dy * dy + dz * dz;
  if (rho2 > 1.0f) rho2 = 1.0f; // kutu disina tasan kose: yaricapta say
  const float d2 = rho2 * b.radius * b.radius;
  return b.intensity / (d2 + 1.0f); // her zaman > 0: bolme guvenli, siralama kararli
}
uint32_t bit_index(uint32_t m) { // en dusuk set bitin indeksi (m != 0)
  uint32_t i = 0;
  while (!(m & 1u)) { m >>= 1; i++; }
  return i;
}
uint32_t cluster_phase(uint32_t i) {
  uint32_t h = i * 2654435761u;
  h ^= h >> 15;
  return h;
}
} // namespace

void cluster_assign(const Mat4 &view, const Mat4 &proj, const PointLight *lights, uint32_t n, const ClusterGrid &g,
                    uint32_t *masks, ClusterStats *stats, const StochasticConfig *sc, uint32_t *stoch) {
  std::memset(masks, 0, sizeof(uint32_t) * g.count());
  ClusterStats st{};
  const uint32_t nl = n > 32 ? 32 : n;
  const bool want_stoch = sc && sc->budget > 0 && stoch;
  LightBox box_store[32]{};
  LightBox *boxes = want_stoch ? box_store : nullptr;
  for (uint32_t li = 0; li < nl; li++) {
    const PointLight &L = lights[li];
    if (L.radius <= 0.0f || L.intensity <= 0.0f) continue;
    Vec4 c4 = view * Vec4{L.pos.x, L.pos.y, L.pos.z, 1.0f};
    const Vec3 c{c4.x, c4.y, c4.z};
    const float r = L.radius;
    // Derinlik araligi (view -z ileri)
    const float dmin = -c.z - r, dmax = -c.z + r;
    if (dmax <= g.znear || dmin >= g.zfar) continue; // tumuyle disarida
    uint32_t s0 = cluster_slice_of(g, dmin < g.znear ? g.znear : dmin);
    uint32_t s1 = cluster_slice_of(g, dmax > g.zfar ? g.zfar : dmax);
    // Ekran araligi: AABB koselerini projekte et; near gerisine tasarsa tum ekran.
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    bool full = false;
    for (int k = 0; k < 8 && !full; k++) {
      Vec3 p{c.x + ((k & 1) ? r : -r), c.y + ((k & 2) ? r : -r), c.z + ((k & 4) ? r : -r)};
      Vec4 q = proj * Vec4{p.x, p.y, p.z, 1.0f};
      if (q.w <= 1e-5f) { full = true; break; }
      float nx = q.x / q.w, ny = q.y / q.w;
      if (nx < x0) x0 = nx; if (nx > x1) x1 = nx;
      if (ny < y0) y0 = ny; if (ny > y1) y1 = ny;
    }
    int tx0 = 0, tx1 = (int)g.x - 1, ty0 = 0, ty1 = (int)g.y - 1;
    if (!full) {
      if (x1 < -1.0f || x0 > 1.0f || y1 < -1.0f || y0 > 1.0f) continue; // ekran disi
      auto tile = [](float ndc, uint32_t tiles) {
        float t = (ndc * 0.5f + 0.5f) * (float)tiles;
        if (t < 0) t = 0;
        if (t > (float)tiles - 1) t = (float)tiles - 1;
        return (int)t;
      };
      tx0 = tile(x0, g.x); tx1 = tile(x1, g.x);
      ty0 = tile(y0, g.y); ty1 = tile(y1, g.y); // NDC y asagi = framebuffer y asagi (proj y'yi cevirdi)
    }
    if (boxes) boxes[li] = LightBox{tx0, tx1, ty0, ty1, s0, s1, L.intensity, r};
    const uint32_t bit = 1u << li;
    bool any = false;
    for (uint32_t s = s0; s <= s1; s++)
      for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
          masks[(s * g.y + (uint32_t)ty) * g.x + (uint32_t)tx] |= bit;
          any = true;
        }
    if (any) st.lights_visible++;
  }
  // --- Stokastik secim (tile basina onem agirlikli alt kume) ---------------
  if (want_stoch) {
    std::memset(stoch, 0, sizeof(uint32_t) * 2 * g.count());
    const uint32_t budget = sc->budget;
    // En az bir ornek kuyruga kalmali; yoksa "stokastik" degil, sadece kirpma
    // olurdu ve kuyruk enerjisi TEMELLI kaybolurdu (yanli tahmin edici).
    const uint32_t keep_min = sc->keep < budget ? sc->keep : (budget ? budget - 1 : 0);
    for (uint32_t z = 0; z < g.z; z++)
      for (uint32_t y = 0; y < g.y; y++)
        for (uint32_t x = 0; x < g.x; x++) {
          const uint32_t ci = (z * g.y + y) * g.x + x;
          uint32_t m = masks[ci];
          if (!m) continue;
          uint32_t idx[32];
          float imp[32];
          uint32_t cnt = 0;
          while (m) {
            const uint32_t i = bit_index(m);
            m &= m - 1;
            idx[cnt] = i;
            imp[cnt] = box_importance(box_store[i], x, y, z);
            cnt++;
          }
          if (cnt <= budget) continue; // butce yetiyor: hepsi degerlendirilir
          // Onem sirasi (azalan). Esitlikte isik indeksi karar verir: ayni
          // girdi -> ayni sira, yani kareler arasi sira titremesi YOK.
          for (uint32_t a = 1; a < cnt; a++) {
            const uint32_t ki = idx[a];
            const float kv = imp[a];
            uint32_t b2 = a;
            while (b2 > 0 && (imp[b2 - 1] < kv || (imp[b2 - 1] == kv && idx[b2 - 1] > ki))) {
              imp[b2] = imp[b2 - 1]; idx[b2] = idx[b2 - 1]; b2--;
            }
            imp[b2] = kv; idx[b2] = ki;
          }
          const uint32_t keep = keep_min;
          const uint32_t samples = budget - keep;
          const uint32_t tail_total = cnt - keep;
          uint32_t sel = 0, tail_mask = 0;
          for (uint32_t k = 0; k < keep; k++) sel |= 1u << idx[k]; // CAPA
          // KATMANLI DONME: esit araliklarla sec, deseni kare + kume fazi kadar
          // kaydir. (j * tail_total) / samples j'de kesin artan oldugu icin
          // secilenler tekrarsizdir.
          float tail_sum = 0;
          for (uint32_t k = keep; k < cnt; k++) tail_sum += imp[k];
          // ONEME ORANTILI (PPS) KATMANLI ORNEKLEME. Sirali kuyrugun onem
          // toplami `samples` esit dilime bolunur ve her dilimden CDF'in
          // kestigi isik alinir. Yani secilme olasiligi ONEMLE ORANTILIDIR:
          // guclu isik hemen her kare secilir, zayifi seyrek. Rank uzerinde
          // DUZGUN ornekleme (ilk deneme) bunun tersini yapiyordu — zayif isigi
          // guclusuyle esit siklikta secip agirligini 9 kata cikariyordu; hem
          // gurultuyu hem de (ust sinir kirpmasi yuzunden) %30 sistematik
          // karartmayi o uretti (olculdu).
          const float step = tail_sum / (float)samples;
          const uint32_t phases = sc->phases ? sc->phases : 1u;
          const float frac = (float)((sc->frame + cluster_phase(ci)) % phases) / (float)phases;
          float acc = 0, sel_sum = 0;
          uint32_t k2 = keep;
          for (uint32_t j = 0; j < samples; j++) {
            const float u = step * (frac + (float)j);
            while (k2 + 1 < cnt && acc + imp[k2] < u) { acc += imp[k2]; k2++; }
            const uint32_t bit2 = 1u << idx[k2];
            if (!(tail_mask & bit2)) sel_sum += imp[k2]; // ayni isik iki dilime dusebilir
            sel |= bit2;
            tail_mask |= bit2;
          }
          // TELAFI: ORAN tahmin edicisi — agirlik SAYIYA degil ENERJIYE gore.
          //   w = (kuyrugun toplam onemi) / (secilenlerin toplam onemi)
          // Katki onemle orantiliysa (tasarim geregi oyle: onem sonum
          // penceresinin ta kendisi) tahmin TAM olur ve kareler arasi degisim
          // birbirini goturur: secilenler zayifsa w buyur, gucluyse kucultur.
          // Sayiya dayali (kuyruk/ornek) agirlik bunu yapamaz; zayif bir isigi
          // 10 kat buyutup guclusunu 10 kat kucultur, yani titremeyi URETIR.
          // TELAFI: oran tahmin edicisi. PPS secimiyle birlikte w tipik olarak
          // 1..2 arasindadir (secilenler zaten kuyruk enerjisinin cogunu tutar).
          // UST SINIR YOK: kirpma yalniz BUYUK agirliklari keserdi, kucukleri
          // yukseltmezdi — yani tahmin ediciyi sistematik olarak asagi yanli
          // yapardi (olculdu: %30 karartma). Guvenlik, onemdeki 1e-4 tabaniyla
          // saglanir (sel_sum hicbir zaman 0 olmaz).
          float w = (sel_sum > 1e-12f && tail_sum > 0.0f) ? tail_sum / sel_sum : 1.0f;
          if (!(w > 0.0f)) w = 1.0f;
          if (!sc->compensate) w = 1.0f; // KONTROL kipi: telafi yok
          if (w > st.max_weight) st.max_weight = w;
          masks[ci] = sel;
          stoch[ci * 2 + 0] = tail_mask;
          std::memcpy(&stoch[ci * 2 + 1], &w, sizeof w); // float bit deseni
          st.clusters_sampled++;
          st.lights_dropped += tail_total - samples;
        }
  }
  if (stats) {
    for (uint32_t i = 0; i < g.count(); i++) {
      if (!masks[i]) continue;
      st.clusters_touched++;
      uint32_t m = masks[i], cnt = 0;
      while (m) { cnt++; m &= m - 1; }
      if (cnt > st.max_lights_in_cluster) st.max_lights_in_cluster = cnt;
      st.lights_evaluated += cnt;
    }
    // Secim olmasaydi: degerlendirilenler + elenenler.
    st.lights_evaluated_full = st.lights_evaluated + st.lights_dropped;
    *stats = st;
  }
}

} // namespace tulpar::engine::renderer
