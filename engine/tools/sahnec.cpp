// engine_sahnec — sahne derleyicisi (PLAN §6 / Faz 6): yazar metni (.sahne) ->
// runtime blob (.sahneb) + icerik paketi (.tpak).
//   engine_sahnec in.sahne [out.sahneb]        derle: blob + yerlesik kume olcumu
//                                              + navmesh bake (--hizli: yalniz blob)
//   engine_sahnec --check in.sahne             derle, ac, ozeti bas (dosya yazmaz)
//   engine_sahnec --dump x.sahneb              blob'u dogrula ve tablolari bas
//   engine_sahnec --kanonik in.sahne [out]     metni KANONIK bicimde yeniden yaz
//                                              (elle yazilan sahneyi yazicinin bicimine sokar)
//   engine_sahnec --pack out.tpak [ad=]dosya...  blok adreslenebilir pack yaz
//   engine_sahnec --pack-dok x.tpak            pack dizinini bas + butunluk dogrula
//   engine_sahnec --yama eski.tpak yeni.tpak cikti.tyama        delta yama yaz
//   engine_sahnec --yama-uygula eski.tpak yama.tyama cikti.tpak yamayi uygula
// Secenekler: --hizli (olcum/bake yok), --onbellek <dizin>, --onbelleksiz,
//             --gi (GI sonda bake'i; KAPALI varsayilan, yavas), --gi-isin N,
//             --gi-adim M, --gi-sicrama N (0 = sicrama yok), --gi-modelsiz.
// Cikis: 0 basari, 1 hata, 2 kullanim.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/importer.hpp"
#include "content/pack.hpp"
#include "content/scene.hpp"
#include "content/cluster_dag.hpp"
#include "content/gi.hpp"
#include "content/scene_blob.hpp"
#include "content/scene_compile.hpp"
#include "core/memory/arena.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

namespace {
void dump(const SceneBlobView &v) {
  const SceneBlobHeader &h = *v.h;
  std::printf("sahne blob v%u: %u bayt, ozet %016llx\n", h.version, h.total_size, (unsigned long long)v.hash());
  std::printf("  dunya: gunes (%g %g %g) x%g ortam (%g %g %g) golge (%g %g %g) r=%g d=%g kamera (%g %g %g) yaw=%g pitch=%g r=%g\n",
              h.sun_dir[0], h.sun_dir[1], h.sun_dir[2], h.sun_diffuse, h.ambient[0], h.ambient[1], h.ambient[2], h.shadow_center[0],
              h.shadow_center[1], h.shadow_center[2], h.shadow_radius, h.shadow_depth, h.cam_target[0], h.cam_target[1], h.cam_target[2],
              h.cam_yaw, h.cam_pitch, h.cam_radius);
  std::printf("  sinir: (%g %g %g) - (%g %g %g)\n", h.bounds_lo[0], h.bounds_lo[1], h.bounds_lo[2], h.bounds_hi[0], h.bounds_hi[1], h.bounds_hi[2]);
  std::printf("  tablolar: kaynak %u @%u, varlik %u @%u, cizim %u @%u, animasyon %u @%u, isik %u @%u, govde %u @%u, metin %u @%u\n",
              h.asset_count, h.asset_offset, h.entity_count, h.entity_offset, h.draw_count, h.draw_offset, h.anim_count, h.anim_offset,
              h.light_count, h.light_offset, h.body_count, h.body_offset, h.string_size, h.string_offset);
  std::printf("  yerlesik kume: CPU %u B, GPU %u B, gecici (en buyuk blok) %u B, tepe %u B%s\n", h.resident_cpu, h.resident_gpu,
              h.peak_transient, h.peak_bytes, (h.budget_flags & 1u) ? " [EKSIK: bir kaynak acilamadi]" : "");
  for (uint32_t i = 0; i < h.resident_count; i++) {
    const SceneBlobResident &r = v.residents[i];
    std::printf("    kaynak %u \"%s\": CPU %u B, vertex %u B, indeks %u B, doku %u B, klip %u B, gecici %u B%s\n", r.asset,
                r.asset < h.asset_count ? v.asset_path(r.asset) : "?", r.cpu_bytes, r.gpu_vertex, r.gpu_index, r.gpu_texture, r.gpu_clip,
                r.transient, (r.flags & 1u) ? " [acilamadi]" : "");
  }
  if (h.nav_size)
    std::printf("  navmesh: %u bayt @%u, %u poligon, %u vertex (girdi %u ucgen; ajan r=%g h=%g, hucre %g, tirmanma %g)\n", h.nav_size,
                h.nav_offset, h.nav_polys, h.nav_verts, h.nav_tris, h.nav_agent_radius, h.nav_agent_height, h.nav_cell_size,
                h.nav_agent_climb);
  else std::printf("  navmesh: yok (bake edilmedi)\n");
  if (h.dag_mesh_count) {
    std::printf("  kume DAG: %u mesh dilimi, %u dugum @%u, %u indeks, %u cocuk, %u seviye, cihaz sinifi %s\n", h.dag_mesh_count,
                h.dag_node_count, h.dag_node_offset, h.dag_index_count, h.dag_child_count, h.dag_levels,
                device_class_name((DeviceClass)h.dag_device_class));
    for (uint32_t i = 0; i < h.dag_mesh_count; i++) {
      const SceneBlobDagMesh &dm = v.dag_meshes[i];
      uint32_t top = 0;
      for (uint32_t n = 0; n < dm.node_count; n++)
        if (v.dag_nodes[dm.node_first + n].level == dm.levels - 1) top += v.dag_nodes[dm.node_first + n].index_count / 3;
      std::printf("    kaynak %u mesh %u: %u kume, %u seviye, %u ucgen -> en ust %u ucgen\n", dm.asset, dm.mesh, dm.node_count,
                  dm.levels, dm.index_count / 3, top);
    }
  } else std::printf("  kume DAG: yok (--kume ile bake edilir)\n");
  if (h.gi_probe_count) {
    std::printf("  GI sondalari: %u sonda (%ux%ux%u, adim %g), %u gecerli / %u kati icinde, baslangic (%g %g %g)\n", h.gi_probe_count,
                h.gi_dim[0], h.gi_dim[1], h.gi_dim[2], h.gi_spacing, h.gi_valid, h.gi_probe_count - h.gi_valid, h.gi_origin[0],
                h.gi_origin[1], h.gi_origin[2]);
    std::printf("    ayar: %u isin/sonda, %u sicrama, bayraklar 0x%x (%s%s%s%s), %u bayt\n", h.gi_rays, h.gi_bounces, h.gi_flags,
                (h.gi_flags & kGiDirectSun) ? "dogrudan-gunes " : "", (h.gi_flags & kGiPointLights) ? "nokta-isik " : "",
                (h.gi_flags & kGiModelTris) ? "model-ucgen " : "", (h.gi_flags & kGiBounce) ? "sicrama" : "sicramasiz",
                (uint32_t)(h.gi_probe_count * sizeof(SceneBlobGiProbe)));
    // Sonda 0'in ve en parlak sondanin +Y yuzu (gozle dogrulama icin).
    uint32_t best = 0;
    float best_l = -1;
    for (uint32_t i = 0; i < h.gi_probe_count; i++) {
      const SceneBlobGiProbe &pr = v.gi_probes[i];
      if (pr.flags & 1u) continue;
      const float l = 0.2126f * pr.face[2][0] + 0.7152f * pr.face[2][1] + 0.0722f * pr.face[2][2];
      if (l > best_l) { best_l = l; best = i; }
    }
    if (best_l >= 0) {
      const SceneBlobGiProbe &pr = v.gi_probes[best];
      std::printf("    en parlak sonda %u: +Y (%g %g %g), -Y (%g %g %g), gunes gorunurlugu %g\n", best, pr.face[2][0], pr.face[2][1],
                  pr.face[2][2], pr.face[3][0], pr.face[3][1], pr.face[3][2], pr.sun_vis);
    }
  } else std::printf("  GI sondalari: yok (--gi ile bake edilir)\n");
  for (uint32_t i = 0; i < h.asset_count; i++) std::printf("  kaynak %u: \"%s\"\n", i, v.asset_path(i));
  for (uint32_t i = 0; i < h.entity_count; i++) {
    const SceneBlobEntity &e = v.entities[i];
    std::printf("  varlik %u \"%s\": konum (%g %g %g) olcek (%g %g %g) bilesen 0x%x", i, v.entity_name(i), e.pos[0], e.pos[1], e.pos[2],
                e.scale[0], e.scale[1], e.scale[2], e.components);
    if (e.draw >= 0) std::printf(" cizim[%d]=kaynak %u", e.draw, v.draws[e.draw].asset);
    if (e.anim >= 0) std::printf(" anim[%d]=klip %u faz %g hiz %g", e.anim, v.anims[e.anim].clip, v.anims[e.anim].phase, v.anims[e.anim].speed);
    if (e.light >= 0) std::printf(" isik[%d]=(%g %g %g) x%g r=%g", e.light, v.lights[e.light].color[0], v.lights[e.light].color[1],
                                  v.lights[e.light].color[2], v.lights[e.light].intensity, v.lights[e.light].radius);
    if (e.body >= 0) std::printf(" govde[%d]=%s %s", e.body, v.bodies[e.body].shape == 0 ? "kutu" : "kure", v.bodies[e.body].dynamic ? "dinamik" : "sabit");
    std::printf("\n");
  }
}

void report_compile(const SceneCompileReport &rep, bool cache_on) {
  std::printf("  yerlesik kume: %u kaynak olculdu%s — CPU %u B, GPU %u B, gecici %u B, tepe %u B\n", rep.assets_measured,
              rep.assets_missing ? " (EKSIK kaynak var)" : "", rep.resident_cpu, rep.resident_gpu, rep.peak_transient, rep.peak_bytes);
  if (cache_on) std::printf("  onbellek: %u isabet (is atlandi), %u iska\n", rep.cache_hits, rep.cache_misses);
  if (rep.nav_ok)
    std::printf("  navmesh: %u ucgen -> %u poligon / %u vertex, %u bayt, ozet %016llx\n", rep.nav_tris, rep.nav_polys, rep.nav_verts,
                rep.nav_bytes, (unsigned long long)rep.nav_hash);
  else std::printf("  navmesh: %s\n", rep.nav_error[0] ? rep.nav_error : "bake kapali");
  if (rep.gi_ok)
    std::printf("  GI: %u sonda (%ux%ux%u, adim %g), %u gecerli, %u tikayici + %u ucgen (%u BVH dugumu), %llu isin, %.2f s, %u B, "
                "isima %g..%g, ozet %016llx\n",
                rep.gi.probes, rep.gi.dim[0], rep.gi.dim[1], rep.gi.dim[2], rep.gi.spacing, rep.gi.probes - rep.gi.probes_inside,
                rep.gi.occluders, rep.gi.triangles, rep.gi.bvh_nodes, (unsigned long long)rep.gi.rays, rep.gi.seconds, rep.gi_bytes,
                rep.gi.min_luma, rep.gi.max_luma, (unsigned long long)rep.gi.hash);
  if (rep.dag_meshes)
    std::printf("  kume DAG: %u mesh, %u kume, %u seviye, en ust %u ucgen, %u B (+%u indeks, %u cocuk), ozet %016llx\n",
                rep.dag_meshes, rep.dag_nodes, rep.dag_levels, rep.dag_top_tris, rep.dag_bytes, rep.dag_indices, rep.dag_children,
                (unsigned long long)rep.dag_hash);
}

uint64_t file_bytes(const char *path) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return 0;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fclose(f);
  return n > 0 ? (uint64_t)n : 0;
}
const char *base_name(const char *path) {
  const char *s = std::strrchr(path, '/');
  return s ? s + 1 : path;
}
uint32_t type_of(const char *name) {
  const char *dot = std::strrchr(name, '.');
  if (!dot) return kPackRaw;
  if (!std::strcmp(dot, ".sahneb")) return kPackScene;
  if (!std::strcmp(dot, ".ktx2")) return kPackTexture;
  if (!std::strcmp(dot, ".gltf") || !std::strcmp(dot, ".glb")) return kPackModel;
  if (!std::strcmp(dot, ".wav") || !std::strcmp(dot, ".ogg")) return kPackAudio;
  return kPackRaw;
}
const char *type_name(uint32_t t) {
  switch (t) {
  case kPackScene: return "sahne";
  case kPackModel: return "model";
  case kPackTexture: return "doku";
  case kPackNav: return "navmesh";
  case kPackAudio: return "ses";
  default: return "ham";
  }
}

int cmd_pack(Arena &sys, int argc, char **argv) {
  if (argc < 4) { std::fprintf(stderr, "--pack out.tpak [ad=]dosya...\n"); return 2; }
  const char *out = argv[2];
  PackWriter w;
  if (!w.init(sys, 256, 32u << 10)) { std::fprintf(stderr, "arena\n"); return 1; }
  for (int i = 3; i < argc; i++) {
    const char *arg = argv[i];
    if (!std::strcmp(arg, "--onbellek")) { i++; continue; } // secenek + degeri
    if (arg[0] == '-') continue;                            // diger secenekler
    char name[256];
    const char *path = arg;
    const char *eq = std::strchr(arg, '=');
    if (eq) {
      const size_t n = (size_t)(eq - arg);
      if (n >= sizeof name) { std::fprintf(stderr, "ad cok uzun: %s\n", arg); return 1; }
      std::memcpy(name, arg, n);
      name[n] = 0;
      path = eq + 1;
    } else std::snprintf(name, sizeof name, "%s", base_name(arg));
    if (!w.add_file(sys, name, type_of(name), path)) { std::fprintf(stderr, "eklenemedi: %s (%s)\n", path, name); return 1; }
  }
  if (!w.count()) { std::fprintf(stderr, "pack bos\n"); return 1; }
  if (!w.save(sys, out)) { std::fprintf(stderr, "yazilamadi: %s\n", out); return 1; }
  // Yazdiktan sonra mmap ile geri ac ve butun bloklari dogrula (olcum, iddia degil).
  PackFile pf;
  PackError perr;
  if (!pack_open(out, &pf, &perr)) { std::fprintf(stderr, "%s: %s\n", out, perr.msg); return 1; }
  uint32_t bad = 0;
  const bool ok = pack_verify_all(pf, &bad);
  std::printf("%s: %u varlik, %zu bayt%s\n", out, pf.count(), pf.size, ok ? "" : " [BOZUK BLOK]");
  for (uint32_t i = 0; i < pf.count(); i++) {
    const PackEntry &e = pf.entries[i];
    std::printf("  %-28s %-8s @%-10llu %8llu B  ozet %016llx\n", pf.name(e), type_name(e.type), (unsigned long long)e.offset,
                (unsigned long long)e.size, (unsigned long long)e.content_hash);
  }
  pack_close(&pf);
  return ok ? 0 : 1;
}

int cmd_patch(Arena &sys, int argc, char **argv) {
  if (argc < 5) { std::fprintf(stderr, "--yama eski.tpak yeni.tpak cikti.tyama\n"); return 2; }
  PackFile a, b;
  PackError err;
  if (!pack_open(argv[2], &a, &err)) { std::fprintf(stderr, "%s: %s\n", argv[2], err.msg); return 1; }
  if (!pack_open(argv[3], &b, &err)) { std::fprintf(stderr, "%s: %s\n", argv[3], err.msg); pack_close(&a); return 1; }
  uint32_t changed = 0;
  uint64_t bytes = 0;
  const bool ok = pack_patch_create(sys, a, b, argv[4], &changed, &bytes);
  if (ok)
    std::printf("%s: %u/%u blok degisti, %llu bayt tasiniyor (yeni pack %zu bayt, yama %llu bayt)\n", argv[4], changed, b.count(),
                (unsigned long long)bytes, b.size, (unsigned long long)file_bytes(argv[4]));
  else std::fprintf(stderr, "yama yazilamadi: %s\n", argv[4]);
  pack_close(&a);
  pack_close(&b);
  return ok ? 0 : 1;
}

int cmd_patch_apply(Arena &sys, int argc, char **argv) {
  if (argc < 5) { std::fprintf(stderr, "--yama-uygula eski.tpak yama.tyama cikti.tpak\n"); return 2; }
  PackFile a, p;
  PackError err;
  if (!pack_open(argv[2], &a, &err)) { std::fprintf(stderr, "%s: %s\n", argv[2], err.msg); return 1; }
  if (!pack_open(argv[3], &p, &err)) { std::fprintf(stderr, "%s: %s\n", argv[3], err.msg); pack_close(&a); return 1; }
  const bool ok = pack_patch_apply(sys, a, p, argv[4], &err);
  if (ok) std::printf("%s: yama uygulandi (%llu bayt)\n", argv[4], (unsigned long long)file_bytes(argv[4]));
  else std::fprintf(stderr, "%s\n", err.msg);
  pack_close(&a);
  pack_close(&p);
  return ok ? 0 : 1;
}

int cmd_pack_dump(const char *path) {
  PackFile pf;
  PackError perr;
  if (!pack_open(path, &pf, &perr)) { std::fprintf(stderr, "%s: %s\n", path, perr.msg); return 1; }
  uint32_t bad = 0;
  const bool ok = pack_verify_all(pf, &bad);
  std::printf("pack v%u: %u varlik, %zu bayt, dizin @%u, veri @%u%s\n", pf.h->version, pf.count(), pf.size, pf.h->entry_offset,
              pf.h->data_offset, ok ? "" : " [BOZUK BLOK]");
  for (uint32_t i = 0; i < pf.count(); i++) {
    const PackEntry &e = pf.entries[i];
    std::printf("  %-28s %-8s @%-10llu %8llu B  ozet %016llx%s\n", pf.name(e), type_name(e.type), (unsigned long long)e.offset,
                (unsigned long long)e.size, (unsigned long long)e.content_hash, (!ok && bad == i) ? "  <-- BOZUK" : "");
  }
  pack_close(&pf);
  return ok ? 0 : 1;
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "kullanim: engine_sahnec in.sahne [out.sahneb] | --check in.sahne | --dump x.sahneb | --kanonik in.sahne [out]\n"
                 "          engine_sahnec --pack out.tpak [ad=]dosya... | --pack-dok x.tpak\n"
                 "          engine_sahnec --yama eski.tpak yeni.tpak cikti.tyama | --yama-uygula eski.tpak yama.tyama cikti.tpak\n"
                 "secenekler: --hizli (olcum/navmesh bake yok), --onbellek <dizin>, --onbelleksiz,\n"
                 "            --kume[=dusuk|orta|yuksek] (kume DAG bake; cihaz sinifina gore),\n"
                 "            --gi (GI sonda bake'i), --gi-isin N, --gi-adim M, --gi-sicrama N, --gi-modelsiz\n");
    return 2;
  }
  static SystemArena sys;
  if (!sys.reserve(512u << 20, "sahnec")) { std::fprintf(stderr, "arena\n"); return 1; }
  bool fast = false, use_cache = true, dag = false, gi = false, gi_models = true;
  DeviceClass dag_class = DeviceClass::Mid;
  SceneGiOptions gi_opt;
  const char *cache_dir = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--hizli")) fast = true;
    else if (!std::strcmp(argv[i], "--onbelleksiz")) use_cache = false;
    else if (!std::strcmp(argv[i], "--onbellek") && i + 1 < argc) cache_dir = argv[++i];
    else if (!std::strcmp(argv[i], "--gi")) gi = true;
    else if (!std::strcmp(argv[i], "--gi-modelsiz")) gi_models = false;
    else if (!std::strcmp(argv[i], "--gi-isin") && i + 1 < argc) { gi = true; gi_opt.rays = (uint32_t)std::atoi(argv[++i]); }
    else if (!std::strcmp(argv[i], "--gi-adim") && i + 1 < argc) { gi = true; gi_opt.spacing = (float)std::atof(argv[++i]); }
    else if (!std::strcmp(argv[i], "--gi-sicrama") && i + 1 < argc) { gi = true; gi_opt.bounces = (uint32_t)std::atoi(argv[++i]); }
    else if (!std::strncmp(argv[i], "--kume", 6)) {
      dag = true;
      const char *eq = std::strchr(argv[i], '=');
      if (eq && !std::strcmp(eq + 1, "dusuk")) dag_class = DeviceClass::Low;
      else if (eq && !std::strcmp(eq + 1, "yuksek")) dag_class = DeviceClass::High;
      else if (eq && std::strcmp(eq + 1, "orta")) { std::fprintf(stderr, "bilinmeyen cihaz sinifi: %s (dusuk|orta|yuksek)\n", eq + 1); return 2; }
    }
  }
  SceneError err{};
  if (!std::strcmp(argv[1], "--dump")) {
    if (argc < 3) { std::fprintf(stderr, "--dump x.sahneb\n"); return 2; }
    SceneBlobView v;
    if (!scene_blob_load(sys, argv[2], &v, &err)) { std::fprintf(stderr, "%s: %s\n", argv[2], err.msg); return 1; }
    dump(v);
    return 0;
  }
  if (!std::strcmp(argv[1], "--pack")) return cmd_pack(sys, argc, argv);
  if (!std::strcmp(argv[1], "--yama")) return cmd_patch(sys, argc, argv);
  if (!std::strcmp(argv[1], "--yama-uygula")) return cmd_patch_apply(sys, argc, argv);
  if (!std::strcmp(argv[1], "--pack-dok")) {
    if (argc < 3) { std::fprintf(stderr, "--pack-dok x.tpak\n"); return 2; }
    return cmd_pack_dump(argv[2]);
  }
  if (!std::strcmp(argv[1], "--kanonik")) {
    // Elle yazilan .sahne dosyasini oku ve YAZICININ cikardigi bicimde geri yaz:
    // alan sirasi, girinti ve sayi gosterimi kanonik olur (deterministik metin
    // sozlesmesi). Yerinde yazmak guvenli: once ayristirilir, sonra yazilir.
    if (argc < 3) { std::fprintf(stderr, "--kanonik in.sahne [out.sahne]\n"); return 2; }
    const char *in_path = argv[2], *out_path = argc > 3 ? argv[3] : argv[2];
    static SceneDesc d;
    if (!scene_load(sys, in_path, &d, &err)) { std::fprintf(stderr, "%s: %s\n", in_path, err.msg); return 1; }
    if (!scene_save(sys, d, out_path, &err)) { std::fprintf(stderr, "%s: %s\n", out_path, err.msg); return 1; }
    std::printf("%s -> %s: %u varlik, %u kaynak (kanonik)\n", in_path, out_path, d.entity_count, d.asset_count);
    return 0;
  }
  const bool check = !std::strcmp(argv[1], "--check");
  const char *in = check ? (argc > 2 ? argv[2] : nullptr) : argv[1];
  if (!in) { std::fprintf(stderr, "--check in.sahne\n"); return 2; }
  static SceneDesc d;
  if (!scene_load(sys, in, &d, &err)) { std::fprintf(stderr, "%s: %s\n", in, err.msg); return 1; }
  char out[1024];
  if (!check && argc > 2 && argv[2][0] != '-') std::snprintf(out, sizeof out, "%s", argv[2]);
  else if (!scene_blob_path_for(in, out, sizeof out)) { std::fprintf(stderr, "cikti yolu cok uzun\n"); return 1; }

  // Derleyici adimi: kaynaklari olc (ice aktarma onbellegiyle) + navmesh bake.
  ImportCache cache;
  SceneCompileOptions opt;
  opt.measure_resident = !fast;
  opt.bake_nav = !fast;
  opt.build_cluster_dag = dag;
  opt.dag_class = dag_class;
  opt.bake_gi = gi;
  opt.gi_include_models = gi_models;
  opt.gi = gi_opt;
  if (!fast && use_cache && import_cache_init(cache, cache_dir)) opt.cache = &cache;
  char dir[1024];
  scene_dir_of(in, dir, sizeof dir);
  SceneBlobExtras extras;
  SceneCompileReport rep;
  if (!fast && !scene_compile(sys, d, dir, opt, &extras, &rep)) { std::fprintf(stderr, "derleyici adimi basarisiz (arena?)\n"); return 1; }

  const size_t need = scene_blob_compile_ex(d, fast ? nullptr : &extras, nullptr, 0);
  void *buf = sys.alloc(need, kSceneBlobAlign);
  if (!buf) { std::fprintf(stderr, "arena\n"); return 1; }
  scene_blob_compile_ex(d, fast ? nullptr : &extras, buf, need);
  SceneBlobView v;
  if (!scene_blob_open(buf, need, &v, &err)) { std::fprintf(stderr, "derlenen blob acilamadi: %s\n", err.msg); return 1; }
  if (!check) {
    if (!scene_blob_save_ex(sys, d, fast ? nullptr : &extras, out, &err)) { std::fprintf(stderr, "%s: %s\n", out, err.msg); return 1; }
    std::printf("%s -> %s: %u varlik, %u kaynak, %u cizim, %u isik, %u govde, %zu bayt, ozet %016llx\n", in, out, v.h->entity_count,
                v.h->asset_count, v.h->draw_count, v.h->light_count, v.h->body_count, need, (unsigned long long)v.hash());
    if (!fast) report_compile(rep, opt.cache != nullptr);
  } else {
    dump(v);
    if (!fast) report_compile(rep, opt.cache != nullptr);
  }
  return 0;
}
