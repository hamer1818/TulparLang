// L2 RHI — Frame Graph: pass/kaynak bagimlilik grafigi, BUDAMA (kullanilmayan
// pass'lari eleme) + TOPOLOJIK CALISMA SIRASI + kaynak YASAM ARALIGI hesabi.
// VIZYON.md'nin Tier B ("gercek, dogrulanabilir teknik") olarak isaretledigi,
// ama henuz insa EDILMEMIS bir mimari — Frostbite/Unreal RDG/Godot 4'un
// AYNI temel fikri: renderer, "hangi pass hangi kaynagi okur/yazar"i ONCEDEN
// bildirir, GRAF bunlardan CALISMA SIRASINI ve HANGI KAYNAGIN NE ZAMAN
// olu oldugunu CIKARIR. Coğu mobil motor (kucuk ekip, sinirli sure) bunu
// HIC yapmaz -- pass sirasi ELLE, sabit kodlanir.
//
// **DOGRULANDI, VARSAYILMADI:** temel algoritma sekli Granite'in (MIT,
// github.com/Themaister/Granite -- Hans-Kristian Arntzen'in gercek,
// sevkiyat yapmis Vulkan motoru) renderer/render_graph.cpp'sinin GERCEK
// kaynak kodu okunarak karsilastirildi: Granite'in bake() fonksiyonu da
// AYNI iki adimi izler -- (1) "backbuffer"dan (bizim is_output) GERIYE
// DOGRU bagimlilik gecisiyle gereken pass'lari bulup gerisini BUDAMAK
// (traverse_dependencies + filter_passes), (2) bagimliliklara SAYGILI
// bir calisma sirasi CIKARMAK (reorder_passes). Bir fark BULUNDU ve
// BILEREK KORUNDU: Granite'in dongu tespiti sadece "yiginin derinligi
// pass sayisini asarsa dongu VAR say" seklinde bir SEZGI (depend_passes_
// recursive: stack_count > passes.size()) -- ziyaret edilen dugumleri
// ISARETLEMEDIGI icin dallanmasi yuksek (elmas seklinde) ama DONGUSUZ bir
// grafikte bile YANLIS POZITIF verebilir. Bu dosyadaki dfs_visit() UC-
// RENKLI (Unvisited/Visiting/Visited) DFS kullanir -- her dugum EN FAZLA
// BIR KEZ islenir (yanlis pozitif YOK) ve GERCEK geri-kenari (Visiting->
// Visiting) tespit eder -- standart, ders-kitabi-dogru CLRS algoritmasi,
// Granite'in sezgisel kisayolundan DAHA SIKI.
//
// **Somut fayda (mobilde bellek KIT):** resource_lifetime() iki kaynagin
// YASAM ARALIGI KESISMIYORSA ayni GPU bellegini (render target/buffer)
// PAYLASABILECEGINI SOYLER (bellek ALIASLAMA) -- ör. G-buffer'in normal
// tamponu, bloom gecisi bitene kadar geri donusturulebilir. Bu dosya
// YALNIZ CPU tarafi bagimlilik/siralama MANTIGINI cozer (GPU'ya bagimsiz,
// derleyicisiz ortamda TAM hand-trace ile dogrulanabilir); GERCEK Vulkan
// bellek/bariyer yerlestirmesi bunun UZERINE kurulacak SONRAKI adim.
//
// Bilincli kapsam: her kaynak TEK BIR pass tarafindan YAZILIR (coklu-yazim/
// versiyonlama -- ör. ayni tamponun ust uste birden fazla gecisle
// guncellenmesi -- SONRAKI adim, bu ilk surumde REDDEDILIR).
#pragma once
#include <cstdint>

namespace tulpar::engine::rhi {

using FgResourceId = uint32_t;
using FgPassId = uint32_t;
constexpr FgResourceId kFgInvalidResource = UINT32_MAX;
constexpr FgPassId kFgInvalidPass = UINT32_MAX;

class FrameGraph {
 public:
  static constexpr uint32_t kMaxPasses = 64;
  static constexpr uint32_t kMaxResources = 128;
  static constexpr uint32_t kMaxReadsPerPass = 8;

  // name en fazla 31 karakter kopyalanir (kirpilir) -- tanilama/hata ayiklama
  // amaclidir, grafik MANTIGI isme bakmaz (yalniz ID'ler kullanilir).
  FgResourceId create_resource(const char *name);
  // is_output=true: bu pass HER ZAMAN calisir (ör. son swapchain-yazan pass),
  // budama BUNU asla elemez -- diger her canliligi/budamayi BUNDAN geriye
  // dogru cikarir.
  FgPassId add_pass(const char *name, bool is_output = false);

  // AYNI kaynak IKINCI KEZ yazilirsa false doner (coklu-yazim REDDEDILIR --
  // yukaridaki bilincli kapsam sinirlamasi).
  bool pass_writes(FgPassId pass, FgResourceId resource);
  bool pass_reads(FgPassId pass, FgResourceId resource);

  // (1) is_output'tan GERIYE DOGRU ulasilamayan pass'lari BUDAR; (2) kalan
  // pass'lari YAZAN-ONCE-OKUYAN sirasina gore TOPOLOJIK siralar; false:
  // DONGU tespit edildi (grafik calistirilamaz -- pass'lar/reads/writes
  // DUZELTILMEDEN tekrar compile() cagirmak ayni sonucu verir).
  bool compile();

  uint32_t executed_count() const { return order_count_; }
  // i: [0, executed_count()) -- CALISMA SIRASINDA i'inci pass'in ID'si.
  FgPassId executed_pass(uint32_t i) const { return order_[i]; }
  bool was_culled(FgPassId pass) const { return compiled_ && pass < pass_count_ && !live_[pass]; }

  // Basarili compile() SONRASI: [*start,*end] -- executed_pass() SIRA
  // INDEKSLERI cinsinden, kaynagin dogumu (yazildigi pass'in sirasi) ile
  // olumu (en son okundugu pass'in sirasi; hic okunmadiysa dogum sirasinin
  // kendisi). false: kaynak hic yazilmadi YA DA yazan pass BUDANDI (kaynak
  // hic uretilmiyor -- bellek ayirmaya GEREK YOK).
  bool resource_lifetime(FgResourceId resource, uint32_t *start, uint32_t *end) const;

 private:
  enum class VisitState : uint8_t { kUnvisited, kVisiting, kVisited };
  bool dfs_visit(FgPassId p, VisitState *state);
  static void copy_name(char *dst, const char *src);

  struct Pass {
    char name[32] = {0};
    FgResourceId reads[kMaxReadsPerPass] = {};
    uint32_t read_count = 0;
    bool is_output = false;
  };
  struct Resource {
    char name[32] = {0};
    FgPassId producer = kFgInvalidPass;
  };

  Pass passes_[kMaxPasses];
  uint32_t pass_count_ = 0;
  Resource resources_[kMaxResources];
  uint32_t resource_count_ = 0;

  bool live_[kMaxPasses] = {};
  FgPassId order_[kMaxPasses] = {};
  uint32_t order_count_ = 0;
  bool compiled_ = false;
};

} // namespace tulpar::engine::rhi
