// L3 RENDERER — DERLENMIS RENDER GRAPH (ilk surum, plan §6).
//
// Gecisler VERIDIR: ad, tur, girdi/cikti hedefi, mip seviyesi. Tablo KURULUMDA
// bir kez uretilir (graph_build) ve bagimliliklar + layout gecisleri TABLODAN
// TURETILIR (graph_build icindeki ikinci gecis). Kayit aninda cozucu, arama ya
// da ayirma YOKTUR: Renderer::record_* diziyi bastan sona yurur.
//
// Neden boyle: "hangi gecis neyi okur" sorusunun cevabi calisma zamaninda
// aranirsa her kare CPU isi olur ve barrier'lar elle yazilir (unutulur). Burada
// okuyucu/yazici eslesmesi derleme-benzeri bir on adimda cikarilir; bir cikti
// sonradan orneklenmiyorsa graph_validate bunu HATA sayar (olu gecis) ve bir
// girdinin ureticisi yoksa yine hata verir. Gecis eklemek/cikarmak TEK yerde:
// graph_build.
//
// Vulkan tipi ICERMEZ (saf veri): layout niyeti GraphLayout ile tasinir,
// VkImageLayout'a ceviri Renderer'da yapilir.
#pragma once
#include <cstdint>

namespace tulpar::engine::renderer {

// Tablonun ust siniri: cull + golge + hareket + sahne + parlak + (mip-1)
// indirgeme + (mip-1) yukari + birlestirme = 2*mip + 4. kMaxBloomMips = 6 -> 16.
constexpr uint32_t kMaxGraphPasses = 16;
constexpr uint32_t kMaxBloomMips = 6;

enum class PassKind : uint8_t {
  Cull,    // GPU gorunurluk kumeleme (COMPUTE; cikti TAMPON, goruntu degil)
  Shadow,  // golge atlasi (yalniz derinlik, kendi gecisi)
  Motion,  // ekran uzayi hareket vektoru + reactive maske (kendi gecisi)
  Scene,   // sahne -> HDR renk hedefi (depth prepass + renk: 2 subpass)
  Bright,  // parlak gecis (esik): HDR -> down[0]
  Down,    // indirgeme: down[i-1] -> down[i]
  Up,      // toplamali yukari ornekleme: down[i] + (down|up)[i+1] -> up[i]
  Compose, // tam ekran birlestirme (tone + bloom) -> CAGIRANIN hedefi
};

// Graph'in bildigi hedefler. Mip seviyesi ayri alanda tasinir.
enum GraphRes : uint8_t {
  kResShadow = 0, // golge atlasi (derinlik)
  kResHdr = 1,    // sahne HDR renk hedefi
  kResDown = 2,   // bloom indirgeme zinciri (mip'li)
  kResUp = 3,     // bloom yukari zinciri (mip'li)
  kResTarget = 4, // CAGIRANIN hedefi (swapchain/offscreen) — graph bunu yaratmaz
  kResMotion = 5, // hareket vektoru + reactive maske (RG = px kayma, A = maske)
  // Cull gecisinin ciktisi bir GORUNTU degil TAMPON'dur (dolayli komutlar +
  // gorunurluk listesi). Tablo bugun yalniz goruntu hedeflerinin layout'unu
  // turetiyor; bu kaynak orada yer alsin diye var ve tuketicisi (cizim
  // komutlari, surucunun dolayli okumasi) tablo DISINDA oldugu icin gecis
  // out_external isaretlidir — "olu gecis" denetimi onu atlar.
  kResCull = 6,
  kResNone = 0xFF,
};

// Ciktinin gecis sonundaki layout NIYETI (turetilir, elle yazilmaz).
enum class GraphLayout : uint8_t {
  ShaderRead, // sonraki bir gecis ORNEKLIYOR -> SHADER_READ_ONLY_OPTIMAL
  External,   // graph disina cikiyor (cagiranin gecisi ya da golge atlasi)
};

struct GraphPass {
  const char *name = "";
  PassKind kind = PassKind::Scene;
  uint8_t in0 = kResNone, in0_level = 0;
  uint8_t in1 = kResNone, in1_level = 0;
  uint8_t out = kResNone, out_level = 0;
  // --- TURETILEN alanlar (graph_build doldurur; kayitta hesaplanmaz) ---
  uint8_t producer0 = kResNone; // in0'i yazan gecis indeksi
  uint8_t producer1 = kResNone;
  bool out_sampled = false; // ciktisini sonraki bir gecis orneklyor mu
  GraphLayout out_layout = GraphLayout::External;
  // Cikti graph DISINDA tuketiliyor (upscaler / TAA / olcum): "olu gecis"
  // denetimi bunu atlar. Elle KURULUR (turetilemez: tuketici tabloda yok).
  // Bugun yalniz hareket vektoru hedefi boyle; bir upscaler gecisi tabloya
  // girdiginde onun in0'i olur ve bayrak dusmelidir.
  bool out_external = false;
};

struct GraphDesc {
  bool post = false;       // HDR hedefi + bloom zinciri + birlestirme
  bool shadow = true;      // golge gecisi tabloda mi
  bool motion = false;     // hareket vektoru gecisi tabloda mi (Faz 5)
  bool cull = false;       // GPU cull compute gecisi tabloda mi (Faz 9)
  uint32_t bloom_mips = 4; // post acikken 2..kMaxBloomMips
};

namespace detail {
// Ad dizgeleri sabit ve omurluk (tablo const char* tutar).
inline const char *down_name(uint32_t i) {
  static const char *k[kMaxBloomMips] = {"indirge0", "indirge1", "indirge2", "indirge3", "indirge4", "indirge5"};
  return k[i < kMaxBloomMips ? i : kMaxBloomMips - 1];
}
inline const char *up_name(uint32_t i) {
  static const char *k[kMaxBloomMips] = {"yukari0", "yukari1", "yukari2", "yukari3", "yukari4", "yukari5"};
  return k[i < kMaxBloomMips ? i : kMaxBloomMips - 1];
}
} // namespace detail

// Tabloyu uretir ve bagimlilik/layout alanlarini TURETIR. Donus: gecis sayisi
// (0 = kapasite yetmedi).
inline uint32_t graph_build(const GraphDesc &d, GraphPass *out, uint32_t cap) {
  uint32_t mips = d.bloom_mips;
  if (mips < 2) mips = 2;
  if (mips > kMaxBloomMips) mips = kMaxBloomMips;
  const uint32_t need =
      (d.cull ? 1u : 0u) + (d.shadow ? 1u : 0u) + (d.motion ? 1u : 0u) + 1u + (d.post ? (2u * mips) : 0u);
  if (cap < need || cap > kMaxGraphPasses) return 0;
  uint32_t n = 0;
  if (d.cull) {
    // ILK gecis: hem golge hem sahne onun yazdigi dolayli komutlari okur.
    GraphPass p;
    p.name = "cull";
    p.kind = PassKind::Cull;
    p.out = kResCull;
    p.out_external = true; // tuketici cizim komutlari: tabloda gecis degil
    out[n++] = p;
  }
  if (d.shadow) {
    GraphPass p;
    p.name = "golge";
    p.kind = PassKind::Shadow;
    p.out = kResShadow;
    out[n++] = p;
  }
  if (d.motion) {
    // Kendi gecisi (kendi derinligi, transient): sahne gecisine ikinci bir renk
    // eki olarak eklenmedi cunku o zaman mesh.frag'in iki varyanti gerekirdi ve
    // hareket KAPALIYKEN bugunku boru hatlari degisirdi. Ayri gecis: ikinci bir
    // geometri gecisi (olculur, PLAN Faz 5 cihaz diliminde tartilir).
    GraphPass p;
    p.name = "hareket";
    p.kind = PassKind::Motion;
    p.out = kResMotion;
    p.out_external = true; // tuketici (upscaler/TAA) henuz tabloda degil
    out[n++] = p;
  }
  {
    GraphPass p;
    p.name = d.post ? "sahne_hdr" : "sahne";
    p.kind = PassKind::Scene;
    p.in0 = d.shadow ? (uint8_t)kResShadow : (uint8_t)kResNone;
    p.out = d.post ? (uint8_t)kResHdr : (uint8_t)kResTarget;
    out[n++] = p;
  }
  if (d.post) {
    {
      GraphPass p;
      p.name = "parlak";
      p.kind = PassKind::Bright;
      p.in0 = kResHdr;
      p.out = kResDown;
      p.out_level = 0;
      out[n++] = p;
    }
    for (uint32_t i = 1; i < mips; i++) {
      GraphPass p;
      p.name = detail::down_name(i);
      p.kind = PassKind::Down;
      p.in0 = kResDown;
      p.in0_level = (uint8_t)(i - 1);
      p.out = kResDown;
      p.out_level = (uint8_t)i;
      out[n++] = p;
    }
    // Yukari zincir: en kucuk mip'ten baslar. i = mips-2 icin alt kaynak
    // indirgeme zincirinin son mip'i, digerlerinde bir onceki yukari cikti.
    for (uint32_t k = 0; k < mips - 1; k++) {
      const uint32_t i = mips - 2 - k;
      GraphPass p;
      p.name = detail::up_name(i);
      p.kind = PassKind::Up;
      p.in0 = kResDown;
      p.in0_level = (uint8_t)i; // ayni seviye (keskin)
      p.in1 = (i + 1 == mips - 1) ? (uint8_t)kResDown : (uint8_t)kResUp;
      p.in1_level = (uint8_t)(i + 1); // alt seviye (yayilan)
      p.out = kResUp;
      p.out_level = (uint8_t)i;
      out[n++] = p;
    }
    {
      GraphPass p;
      p.name = "birlestir";
      p.kind = PassKind::Compose;
      p.in0 = kResHdr;
      p.in1 = kResUp;
      p.in1_level = 0;
      p.out = kResTarget;
      out[n++] = p;
    }
  }
  // --- TURETME: uretici eslesmesi + layout ---------------------------------
  for (uint32_t j = 0; j < n; j++) {
    const uint8_t ins[2] = {out[j].in0, out[j].in1};
    const uint8_t lvls[2] = {out[j].in0_level, out[j].in1_level};
    for (uint32_t s = 0; s < 2; s++) {
      uint8_t prod = kResNone;
      if (ins[s] != kResNone)
        for (uint32_t i = j; i-- > 0;)
          if (out[i].out == ins[s] && out[i].out_level == lvls[s]) { prod = (uint8_t)i; break; }
      if (s == 0) out[j].producer0 = prod;
      else out[j].producer1 = prod;
      if (prod != kResNone) {
        out[prod].out_sampled = true;
        // Golge atlasi derinliktir ve kendi gecisi finalLayout'u zaten
        // SHADER_READ_ONLY birakir; renk hedefleri de oyle.
        out[prod].out_layout = GraphLayout::ShaderRead;
      }
    }
  }
  return n;
}

// Tutarlilik denetimi. nullptr = tutarli; aksi halde sebep (test kapisi bunu
// POZITIF KONTROL olarak bozuk tabloyla dener).
inline const char *graph_validate(const GraphPass *p, uint32_t n) {
  if (n == 0) return "bos tablo";
  if (n > kMaxGraphPasses) return "gecis sayisi kapasiteyi asiyor";
  for (uint32_t i = 0; i < n; i++) {
    if (p[i].out == kResNone) return "ciktisi olmayan gecis";
    if (p[i].in0 != kResNone && p[i].producer0 == kResNone && p[i].in0 != kResShadow)
      return "ureticisi olmayan girdi (in0)";
    if (p[i].in1 != kResNone && p[i].producer1 == kResNone && p[i].in1 != kResShadow)
      return "ureticisi olmayan girdi (in1)";
    if (p[i].producer0 != kResNone && p[i].producer0 >= i) return "girdi kendinden SONRAKI gecisten geliyor";
    if (p[i].producer1 != kResNone && p[i].producer1 >= i) return "girdi kendinden SONRAKI gecisten geliyor";
    // Cagiranin hedefine yazmayan bir gecisin ciktisi okunmuyorsa olu gecistir.
    // out_external: tuketici graph DISINDA (upscaler/olcum) — olu sayilmaz.
    if (p[i].out != kResTarget && !p[i].out_sampled && !p[i].out_external)
      return "ciktisi hic okunmayan gecis (olu)";
  }
  if (p[n - 1].out != kResTarget) return "son gecis cagiranin hedefine yazmiyor";
  return nullptr;
}

} // namespace tulpar::engine::renderer
