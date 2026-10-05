// BOLUMLU (PARALEL) NESNE URETIMI (2026-10-05).
//
// NEDEN: nesne uretimi (isel + kayit atama + MC) tek is parcaciginda
// kosuyordu ve orta boy bir programda derlemenin ucte biriydi. Olculdu
// (Ryzen 7 9800X3D, LLVM 23, TULPAR_AOT_NOCACHE=1 TULPAR_AOT_TIME=1):
// examples/wings_groups_test.tpr emit-obj 225 ms / toplam 650 ms,
// tests/loop_versioning.test.tpr 303 / 888, benchmarks/fair/nbody.tpr
// 188 / 402. `-time-passes`: isel %37, greedy RA %14, gerisi daginik —
// tek bir pahali gecis yok, is fonksiyon basina dogrusal. Yani bedel
// fonksiyonlara bolunebilir.
//
// NASIL (LTO'nun --lto-partitions yaklasimi, sirayi koruyarak):
//   * Optimizasyon BUTUN modulde kosmus olmali (satir ici acma, GlobalOpt,
//     MergeFunctions modul geneli kararlar) — burada yalniz kod uretimi
//     bolunur, IR'a dokunan tek sey sembol gorunurlugu.
//   * Fonksiyonlar MODUL SIRASIYLA K BITISIK araliga ayrilir (komut
//     sayisina gore dengeli). Global degiskenlerin hepsi SON bolumde ve
//     ozgun sirayla kalir. Nesneler link satirina bolum sirasiyla girer:
//     linker .text'i girdi sirasiyla dizdigi icin fonksiyon yerlesimi tek
//     nesneyle AYNI (Tuzaklar 7i: hizalama farki "kod ayni, program %2
//     yavas" demek; fonksiyon adresleri degismemeli). Kapi:
//     tests/bolumlu_emit.sh adresleri ve komut akisini karsilastirir.
//   * Her bolum KENDI LLVMContext'inde: baglam is parcaciklari arasinda
//     guvenli degil (kod uretimi IR sabitleri yaratiyor). Modul bir kez
//     bitcode'a yazilir; her isci onu TEMBEL okur, yalniz kendi
//     fonksiyonlarinin govdesini acar, otekilerini bildirime cevirir.
//   * Bolumler arasi kullanilan YEREL fonksiyon yerinde yerel kalir; diger
//     bolumler ona gizli (hidden) bir takma ad (alias) uzerinden erisir.
//     Neden takma ad: x86'da ayni bolumdeki yerel hedefe kuyruk atlamasi
//     (`jmp`) birlestirici tarafindan 2 baytlik bicime kisaltilabiliyor;
//     sembol global olsaydi 5 bayt kalirdi ve yerlesim kayardi. Mach-O'da
//     takma ad kullanilmiyor (clang Darwin'de alias'i reddediyor); orada
//     sembol yeniden adlandirilip gizli global yapiliyor — arm64 dal
//     kodlamasi sabit boy, fark cikmaz.
//   * Bolumler arasi kullanilan yerel DEGISKEN tek tanimli kalir, yeniden
//     adlandirilip gizli global olur. Adresi onemsiz (unnamed_addr) yerel
//     SABITLER (dizgi literalleri) kullanan her bolume kopyalanir: linker
//     birlestirilebilir bolumlerde (SHF_MERGE) aynilarini zaten tekler.
//   * Ad cakismasi: disa acilan her ad `__tulpar.b.` onekini alir —
//     nokta iceren bir ad C/C++ runtime'indaki hicbir global sembolle
//     cakisamaz.
//
// Bolum sayisi makineden BAGIMSIZ (komut sayisindan); cekirdek sayisi
// yalniz is parcacigi sayisini belirler. Ayni girdi her makinede ayni
// nesneleri uretir.
//
// ANAHTARLAR: TULPAR_AOT_BOLUM=<n> bolum sayisini zorlar (1 = eski tek
// nesne yolu, bayt bayt eskisi); TULPAR_AOT_ISCI=<n> is parcacigi tavani.
// TULPAR_AOT_BOLUM_SINAMA=ters nesneleri TERS sirayla linkler — yalniz
// tests/bolumlu_emit.sh'nin pozitif kontrolu icin (yerlesim kaymali,
// kapi kirmizi gormeli). TULPAR_AOT_BOLUM_KORU=1 bolum nesnelerini linkten
// sonra silmez (teshis). TULPAR_AOT_TIME=1 [AOT-BOLUM] satirini basar.

#include "llvm_bolum.hpp"

#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Triple.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unordered_map>
#if defined(__linux__)
#include <sched.h>  // sched_getaffinity: taskset/cgroup sinirini gor
#endif

namespace {

// Kullanilabilir cekirdek. std::thread::hardware_concurrency makinenin
// TUM cekirdeklerini sayar; `taskset` ya da cgroup ile kisitlanmis surec
// (CI kapsayicisi, paralel test iscisi) bunu asmamali.
int kullanilabilir_cekirdek() {
#if defined(__linux__)
  cpu_set_t s;
  if (sched_getaffinity(0, sizeof(s), &s) == 0) {
    int n = CPU_COUNT(&s);
    if (n > 0) return n;
  }
#endif
  unsigned h = std::thread::hardware_concurrency();
  return h ? (int)h : 1;
}

// Bir bolumun degdigi komut sayisinin alt siniri. Bolum basina sabit bedel
// (tembel okuma + hedef makinesi + is parcacigi + linkerda bir girdi) ~1-3
// ms; 4000 komut ~25-30 ms kod uretimi (olculdu, wings: ~7 us/komut).
constexpr size_t kBolumBasinaKomut = 4000;
constexpr int kAzamiBolum = 8;

bool env_acik(const char *ad) {
  const char *e = getenv(ad);
  return e && *e && *e != '0';
}

using Maske = uint64_t;  // K <= 64

struct Plan {
  int K = 1;
  int L = 0;                       // global degiskenlerin bolumu (son)
  std::vector<int> fn_bolum;       // modul sirasiyla her fonksiyon; bildirim -1
  std::vector<char> gv_kopya;      // global sirasiyla: 1 = kopyalanabilir sabit
  std::vector<Maske> gv_ev;        // kopyalanabilir sabitin tanimlanacagi bolumler
  // Baska bolumdeki GUCLU tanimin bildirimi dso_local mi isaretlenmeli?
  // Amac: kod uretimi tek nesnedeki TANIMI nasil siniflandirdiysa bildirimi
  // de oyle siniflandirsin (TargetMachine::shouldAssumeDSOLocal):
  //   ELF   : yalniz acik dso_local bayragi sayilir — tanim da bildirim de
  //           ayni bayragi tasir, DOKUNMA (isaretlemek erisimi GOTPCREL'den
  //           mutlak adrese cevirir; olculdu: nbody .text 428 komut kisaldi).
  //   Mach-O: guclu tanim yerel, bildirim degil -> ISARETLE (yoksa GOT).
  //   COFF  : tanim yerel; MinGW'de degisken bildirimi degil -> ISARETLE
  //           (yoksa .refptr dolayliligi).
  bool bildirim_yerel = false;
};

inline Maske bit(int k) { return Maske(1) << k; }

// Adresi onemsiz yerel sabit: her bolume kopyalanabilir. YALNIZ Mach-O'da:
// ELF/COFF'ta disa acilip tek tanimli kalir — boylece dizgi literallerinin
// yerlesimi (ve kodun icindeki mutlak adresleri) tek nesneyle bayt bayt
// ayni olur. Kopyalamada her bolum kendi dizgilerini tasir, linker ayni
// olanlari teklese de SIRA degisir (olculdu: wings, 603 komutta yalniz
// dizgi adresi farki). Mach-O'da ise __cstring'de global sembol ld64'un
// sevmedigi bir bicim; orada kopyalama guvenli yol.
bool kopyalanabilir(const llvm::GlobalVariable &G, bool kopya_kipi) {
  return kopya_kipi && G.hasLocalLinkage() && G.isConstant() && G.hasInitializer() &&
         !G.isThreadLocal() && !G.hasSection() &&
         (G.hasGlobalUnnamedAddr() || G.hasAtLeastLocalUnnamedAddr());
}

std::string bos_ad(llvm::Module &M, llvm::StringRef taban) {
  std::string ad = ("__tulpar.b." + taban).str();
  if (!M.getNamedValue(ad)) return ad;
  for (int i = 1;; i++) {
    std::string a = ad + "." + std::to_string(i);
    if (!M.getNamedValue(a)) return a;
  }
}

// Bir kullanicinin hangi bolumlerde "goruldugu": komut -> fonksiyonunun
// bolumu; global degisken -> tanimlandigi bolumler; sabit ifade -> kendi
// kullanicilarinin birlesimi.
struct Kullanim {
  const std::unordered_map<const llvm::Function *, int> &bolum;
  const std::unordered_map<const llvm::GlobalVariable *, Maske> &ev;  // her global
  int L;
  std::unordered_map<const llvm::Constant *, Maske> bellek;

  Maske kullanici(const llvm::User *U) {
    if (auto *I = llvm::dyn_cast<llvm::Instruction>(U)) {
      auto it = bolum.find(I->getFunction());
      return it == bolum.end() ? ~Maske(0) : bit(it->second);
    }
    if (auto *G = llvm::dyn_cast<llvm::GlobalVariable>(U)) {
      auto it = ev.find(G);
      return it == ev.end() ? bit(L) : it->second;
    }
    if (auto *C = llvm::dyn_cast<llvm::Constant>(U)) {
      if (llvm::isa<llvm::GlobalValue>(C)) return ~Maske(0);  // alias/ifunc: yok sayilmaz
      auto it = bellek.find(C);
      if (it != bellek.end()) return it->second;
      bellek[C] = 0;  // dongu korumasi
      Maske m = kullanicilar(C);
      bellek[C] = m;
      return m;
    }
    return ~Maske(0);  // bilinmeyen kullanici: her yerde say (temkinli)
  }
  Maske kullanicilar(const llvm::Value *V) {
    Maske m = 0;
    for (const llvm::User *U : V->users()) m |= kullanici(U);
    return m;
  }
};

std::string bolum_yolu(const char *dosya, int k) {
  if (k == 0) return dosya;
  std::string d = dosya;
  std::string ek = ".b" + std::to_string(k) + ".o";
  if (d.size() > 2 && d.compare(d.size() - 2, 2, ".o") == 0)
    return d.substr(0, d.size() - 2) + ek;
  return d + ek;
}

struct IsciSonuc {
  std::string hata;
  double ms = 0;
};

// Isci: bitcode'u kendi baglaminda tembel okur, k bolumunu yazar.
void bolum_yaz(const llvm::SmallVector<char, 0> &bc, const Plan &P, int k,
               const std::string &triple, LLVMRelocMode reloc,
               const std::string &yol, IsciSonuc &out) {
  auto t0 = std::chrono::steady_clock::now();
  llvm::LLVMContext Ctx;
  auto ModOrErr = llvm::getLazyBitcodeModule(
      llvm::MemoryBufferRef(llvm::StringRef(bc.data(), bc.size()), "tulpar_bolum"), Ctx);
  if (!ModOrErr) {
    out.hata = "bitcode okunamadi: " + llvm::toString(ModOrErr.takeError());
    return;
  }
  std::unique_ptr<llvm::Module> M = std::move(*ModOrErr);

  // Fonksiyonlar: kendi bolumu acilir, gerisi bildirime doner.
  size_t i = 0;
  for (llvm::Function &F : *M) {
    if (i >= P.fn_bolum.size()) { out.hata = "fonksiyon sayisi tutmuyor"; return; }
    int b = P.fn_bolum[i++];
    if (b < 0) continue;
    if (b == k) {
      if (llvm::Error E = F.materialize()) {
        out.hata = "govde acilamadi: " + llvm::toString(std::move(E));
        return;
      }
    } else {
      // Tanim baska bolumde: bildirime don (dso_local: Plan::bildirim_yerel).
      const bool guclu = !F.isWeakForLinker();
      F.deleteBody();  // ExternalLinkage bildirim
      if (guclu && P.bildirim_yerel) F.setDSOLocal(true);
    }
  }
  if (i != P.fn_bolum.size()) { out.hata = "fonksiyon sayisi tutmuyor"; return; }

  // Takma adlar: hedefi bu bolumde degilse gizli bir bildirime donusur.
  for (llvm::GlobalAlias &A : llvm::make_early_inc_range(M->aliases())) {
    auto *Fn = llvm::dyn_cast<llvm::Function>(A.getAliasee());
    if (!Fn) { out.hata = "beklenmeyen takma ad: " + A.getName().str(); return; }
    if (!Fn->isDeclaration()) continue;
    llvm::Function *D = llvm::Function::Create(
        Fn->getFunctionType(), llvm::GlobalValue::ExternalLinkage,
        Fn->getAddressSpace(), "", M.get());
    D->setCallingConv(Fn->getCallingConv());
    D->setAttributes(Fn->getAttributes());
    D->setVisibility(llvm::GlobalValue::HiddenVisibility);
    D->setDSOLocal(true);
    std::string ad = A.getName().str();
    A.replaceAllUsesWith(D);
    A.eraseFromParent();
    D->setName(ad);
  }

  // Global degiskenler: hepsi L'de; baska bolumde yalniz kopyalanan sabitler
  // tanimli, kalan disa acilmislar bildirim, yerel kullanilmayanlar silinir.
  if (k != P.L) {
    std::vector<llvm::GlobalVariable *> sil;
    size_t g = 0;
    for (llvm::GlobalVariable &G : M->globals()) {
      size_t gi = g++;
      if (G.isDeclaration()) continue;
      if (gi < P.gv_kopya.size() && P.gv_kopya[gi] && (P.gv_ev[gi] & bit(k)))
        continue;
      if (G.hasAppendingLinkage()) { sil.push_back(&G); continue; }
      const bool guclu = !G.isWeakForLinker();
      G.setInitializer(nullptr);
      G.setComdat(nullptr);
      if (G.hasLocalLinkage()) { sil.push_back(&G); continue; }
      G.setLinkage(llvm::GlobalValue::ExternalLinkage);
      if (guclu && P.bildirim_yerel) G.setDSOLocal(true);  // Plan notu
    }
    for (llvm::GlobalVariable *G : sil) {
      // Ilklendiricisi atilan globalin eski sabiti (ornegin dizgi isaretcisi
      // dizisi) baglamda olu olarak yasiyor ve kullanimi hala sayiliyor
      // (olculdu: motor ornegi, @sarr.names -> @sarr.fn).
      G->removeDeadConstantUsers();
      if (!G->use_empty()) {
        out.hata = "yerel global baska bolumde kullaniliyor: " + G->getName().str();
        return;
      }
      G->eraseFromParent();
    }
    // Kullanilmayan global BILDIRIMLERI de sil. Birakilirsa AsmPrinter gizli
    // bildirime `.hidden` yazar ve nesnede tipsiz (NOTYPE) bir tanimsiz
    // sembol kalir; degisken TLS ise ld.bfd "TLS reference mismatches
    // non-TLS reference" ile linki dusurur (olculdu: wings, tpr_g__request).
    for (llvm::GlobalVariable &G : llvm::make_early_inc_range(M->globals())) {
      if (!G.isDeclaration()) continue;
      G.removeDeadConstantUsers();
      if (G.use_empty()) G.eraseFromParent();
    }
  }
  // Artik kimsenin kullanmadigi bildirimler (eski yerel fonksiyonlar dahil).
  for (llvm::Function &F : llvm::make_early_inc_range(*M)) {
    if (!F.isDeclaration() || F.isIntrinsic()) continue;
    F.removeDeadConstantUsers();
    if (F.use_empty()) F.eraseFromParent();
  }

  {
    std::string vhata;
    llvm::raw_string_ostream os(vhata);
    if (llvm::verifyModule(*M, &os)) {
      os.flush();
      out.hata = "bolum dogrulanamadi: " + vhata.substr(0, 400);
      return;
    }
  }

  LLVMTargetRef target = nullptr;
  char *err = nullptr;
  if (LLVMGetTargetFromTriple(triple.c_str(), &target, &err) != 0) {
    out.hata = std::string("hedef yok: ") + (err ? err : "?");
    if (err) LLVMDisposeMessage(err);
    return;
  }
  LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
      target, triple.c_str(), "generic", "", LLVMCodeGenLevelDefault, reloc,
      LLVMCodeModelDefault);
  LLVMModuleRef cm = llvm::wrap(M.get());
  LLVMTargetDataRef td = LLVMCreateTargetDataLayout(tm);
  LLVMSetModuleDataLayout(cm, td);
  LLVMDisposeTargetData(td);
  LLVMSetTarget(cm, triple.c_str());
  if (LLVMTargetMachineEmitToFile(tm, cm, yol.c_str(), LLVMObjectFile, &err) != 0) {
    out.hata = std::string("nesne yazilamadi: ") + (err ? err : "?");
    if (err) LLVMDisposeMessage(err);
  }
  LLVMDisposeTargetMachine(tm);
  out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
               .count();
}

}  // namespace

int tulpar_bolum_sayisi(LLVMModuleRef mod) {
  if (const char *e = getenv("TULPAR_AOT_BOLUM"); e && *e) {
    int n = atoi(e);
    if (n < 1) n = 1;
    if (n > 64) n = 64;
    return n;
  }
  llvm::Module &M = *llvm::unwrap(mod);
  size_t komut = 0, tanimli = 0;
  for (llvm::Function &F : M) {
    if (F.isDeclaration()) continue;
    tanimli++;
    komut += F.getInstructionCount();
  }
  size_t k = komut / kBolumBasinaKomut;
  if (k > (size_t)kAzamiBolum) k = kAzamiBolum;
  if (k > tanimli) k = tanimli;
  return k < 1 ? 1 : (int)k;
}

int tulpar_bolumlu_emit(LLVMModuleRef mod, const char *triple_c,
                        LLVMRelocMode reloc, const char *dosya, int istenen,
                        std::vector<std::string> &nesneler, std::string &hata) {
  auto t0 = std::chrono::steady_clock::now();
  llvm::Module &M = *llvm::unwrap(mod);
  const std::string triple = triple_c;
  const bool macho = llvm::Triple(triple).isOSBinFormatMachO();
  if (!M.alias_empty() || !M.ifunc_empty()) {
    hata = "modulde alias/ifunc var";
    return 2;
  }
  for (llvm::GlobalObject &GO : M.global_objects())
    if (GO.hasComdat()) {
      hata = "modulde comdat var";
      return 2;
    }
  // blockaddress: bir fonksiyonun bloguna baska yerden isaret — govdesi
  // baska bolumde silinirse sabit gecersizlesir. Tulpar kodgeni uretmiyor;
  // uretirse bolmeyiz.
  for (llvm::Function &F : M)
    for (llvm::BasicBlock &BB : F)
      if (BB.hasAddressTaken()) {
        hata = "modulde blockaddress var";
        return 2;
      }

  // 1) Bitisik, dengeli araliklar (modul sirasi).
  std::vector<llvm::Function *> tanimli;
  std::vector<size_t> agirlik;
  size_t toplam = 0;
  for (llvm::Function &F : M) {
    if (F.isDeclaration()) continue;
    size_t w = F.getInstructionCount() + 1;
    tanimli.push_back(&F);
    agirlik.push_back(w);
    toplam += w;
  }
  int K = istenen;
  if ((size_t)K > tanimli.size()) K = (int)tanimli.size();
  if (K < 2) { hata = "bolunecek fonksiyon yok"; return 2; }
  std::vector<int> ham(tanimli.size());
  {
    size_t once = 0;
    for (size_t i = 0; i < tanimli.size(); i++) {
      // Fonksiyonun orta noktasi hangi K'liga dusuyorsa o bolum.
      size_t orta = once + agirlik[i] / 2;
      int b = (int)((orta * (size_t)K) / toplam);
      if (b >= K) b = K - 1;
      if (i > 0 && b < ham[i - 1]) b = ham[i - 1];
      ham[i] = b;
      once += agirlik[i];
    }
  }
  // Bos bolumleri at (dev bir fonksiyon birden cok K'ligi kaplayabilir).
  std::unordered_map<const llvm::Function *, int> bolum;
  std::vector<size_t> bolum_agirlik;
  {
    int son = -1, yeni = -1;
    for (size_t i = 0; i < tanimli.size(); i++) {
      if (ham[i] != son) { son = ham[i]; yeni++; bolum_agirlik.push_back(0); }
      bolum[tanimli[i]] = yeni;
      bolum_agirlik[yeni] += agirlik[i];
    }
    K = yeni + 1;
  }
  if (K < 2) { hata = "tek bolum kaldi"; return 2; }

  Plan P;
  P.K = K;
  P.bildirim_yerel = !llvm::Triple(triple).isOSBinFormatELF();
  P.L = K - 1;
  for (llvm::Function &F : M) {
    auto it = bolum.find(&F);
    P.fn_bolum.push_back(it == bolum.end() ? -1 : it->second);
  }

  // 2) Global degiskenlerin "ev"i: kopyalanabilir sabitler kullanan her
  //    bolumde + L; digerleri yalniz L. Sabitler birbirini kullanabilir —
  //    sabit noktaya kadar yinele.
  std::unordered_map<const llvm::GlobalVariable *, Maske> ev;
  std::vector<llvm::GlobalVariable *> gvler;
  for (llvm::GlobalVariable &G : M.globals()) {
    gvler.push_back(&G);
    ev[&G] = bit(P.L);
  }
  for (int tur = 0; tur < 16; tur++) {
    bool degisti = false;
    Kullanim ku{bolum, ev, P.L, {}};
    for (llvm::GlobalVariable *G : gvler) {
      if (G->isDeclaration() || !kopyalanabilir(*G, macho)) continue;
      Maske m = bit(P.L) | ku.kullanicilar(G);
      if (m == ~Maske(0)) m = bit(P.L);  // bilinmeyen kullanici: kopyalama yok
      if (m != ev[G]) { ev[G] = m; degisti = true; }
    }
    if (!degisti) break;
  }
  for (llvm::GlobalVariable *G : gvler) {
    bool kop = !G->isDeclaration() && kopyalanabilir(*G, macho);
    P.gv_kopya.push_back(kop ? 1 : 0);
    P.gv_ev.push_back(ev[G]);
  }

  // 3) Bolumler arasi yerel kullanim: fonksiyona takma ad (Mach-O'da
  //    yeniden adlandirma), degiskene yeniden adlandirma.
  int takma = 0, disa = 0;
  {
    Kullanim ku{bolum, ev, P.L, {}};
    for (llvm::Function *F : tanimli) {
      if (!F->hasLocalLinkage()) continue;
      int p = bolum[F];
      Maske m = ku.kullanicilar(F);
      if ((m & ~bit(p)) == 0) continue;
      if (macho) {
        F->setName(bos_ad(M, F->getName()));
        F->setLinkage(llvm::GlobalValue::ExternalLinkage);
        F->setVisibility(llvm::GlobalValue::HiddenVisibility);
        F->setDSOLocal(true);
        disa++;
        continue;
      }
      llvm::GlobalAlias *A = llvm::GlobalAlias::create(
          F->getValueType(), F->getAddressSpace(), llvm::GlobalValue::ExternalLinkage,
          bos_ad(M, F->getName()), F, &M);
      A->setVisibility(llvm::GlobalValue::HiddenVisibility);
      A->setDSOLocal(true);
      const Maske ic = bit(p);
      F->replaceUsesWithIf(A, [&](llvm::Use &U) {
        llvm::User *Us = U.getUser();
        if (Us == A) return false;
        return ku.kullanici(Us) != ic;
      });
      // replaceUsesWithIf sabit ifadeleri YENIDEN kurar (eskisi yok edilir);
      // bellekteki sabit isaretcileri bayatladi.
      ku.bellek.clear();
      takma++;
    }
    for (llvm::GlobalVariable *G : gvler) {
      if (G->isDeclaration() || !G->hasLocalLinkage() || kopyalanabilir(*G, macho)) continue;
      Maske m = ku.kullanicilar(G);
      if ((m & ~bit(P.L)) == 0) continue;
      G->setName(bos_ad(M, G->getName()));
      G->setLinkage(llvm::GlobalValue::ExternalLinkage);
      G->setVisibility(llvm::GlobalValue::HiddenVisibility);
      G->setDSOLocal(true);
      disa++;
    }
  }

  // 4) Bir kez bitcode. Kullanim listesi (use-list) SIRASI KORUNUR: kod
  //    uretiminin bazi kararlari kullanim sirasina bakiyor ve okuyucunun
  //    dogal sirasi bellekteki modulunkinden farkli. Olculdu (2026-10-05):
  //    korunmadan tests/loop_versioning.test.tpr'nin .text'i 64 bayt kisa
  //    cikti ve kayma yuzunden ~52 bin komut satiri farkliydi; korununca
  //    bayt bayt ayni. Bedeli ~4 ms (11,6 / 7,6 ms hazirlik).
  llvm::SmallVector<char, 0> bc;
  {
    llvm::raw_svector_ostream os(bc);
    llvm::WriteBitcodeToFile(M, os, /*ShouldPreserveUseListOrder=*/true);
  }
  auto t_hazir = std::chrono::steady_clock::now();

  // 5) Isciler. Ana is parcacigi da calisir; buyuk bolum once (LPT).
  std::vector<std::string> yollar(K);
  for (int k = 0; k < K; k++) yollar[k] = bolum_yolu(dosya, k);
  std::vector<int> sira(K);
  for (int k = 0; k < K; k++) sira[k] = k;
  std::stable_sort(sira.begin(), sira.end(),
                   [&](int a, int b) { return bolum_agirlik[a] > bolum_agirlik[b]; });
  int isci = kullanilabilir_cekirdek();
  if (const char *e = getenv("TULPAR_AOT_ISCI"); e && *e) {
    int n = atoi(e);
    if (n >= 1) isci = std::min(isci, n);
  }
  if (isci > K) isci = K;
  std::vector<IsciSonuc> sonuc(K);
  std::atomic<int> sonraki{0};
  auto calis = [&]() {
    for (;;) {
      int s = sonraki.fetch_add(1);
      if (s >= K) return;
      int k = sira[s];
      bolum_yaz(bc, P, k, triple, reloc, yollar[k], sonuc[k]);
    }
  };
  std::vector<std::thread> ip;
  for (int t = 1; t < isci; t++) {
    // Is parcacigi acilamazsa (kaynak siniri) kalan bolumleri ana is
    // parcacigi siradan yazar — kuyruk zaten ortak.
    try {
      ip.emplace_back(calis);
    } catch (...) {
      break;
    }
  }
  calis();
  for (auto &t : ip) t.join();

  for (int k = 0; k < K; k++) {
    if (!sonuc[k].hata.empty()) {
      hata = "bolum " + std::to_string(k) + ": " + sonuc[k].hata;
      for (int j = 0; j < K; j++) remove(yollar[j].c_str());
      return 1;
    }
  }
  nesneler = yollar;
  if (const char *s = getenv("TULPAR_AOT_BOLUM_SINAMA"); s && strcmp(s, "ters") == 0)
    std::reverse(nesneler.begin(), nesneler.end());

  if (env_acik("TULPAR_AOT_TIME")) {
    auto t1 = std::chrono::steady_clock::now();
    double hazir = std::chrono::duration<double, std::milli>(t_hazir - t0).count();
    double hepsi = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double en_uzun = 0;
    for (auto &r : sonuc) en_uzun = std::max(en_uzun, r.ms);
    // Belirlenimli olcu (kapi bunu okur): en buyuk bolumun komut payi =
    // kod uretiminin paralel kritik yolu. Sure gurultulu, bu degil.
    size_t en_buyuk = 0;
    for (size_t w : bolum_agirlik) en_buyuk = std::max(en_buyuk, w);
    int en_buyuk_pay = (int)((en_buyuk * 100 + toplam / 2) / toplam);
    fprintf(stderr,
            "[AOT-BOLUM] bolum=%d isci=%d komut=%zu en_buyuk_pay=%%%d takma_ad=%d "
            "disa_acilan=%d hazirlik=%.1fms en_uzun_bolum=%.1fms toplam=%.1fms "
            "bitcode=%zuKB\n",
            K, isci, toplam, en_buyuk_pay, takma, disa, hazir, en_uzun, hepsi,
            bc.size() / 1024);
  }
  return 0;
}
