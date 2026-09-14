// Faz 2: animasyon — sikistirma hata sinirlari, sabit iz eleme, ornekleme,
// dongu, model uzayi zinciri, 0 ayirma, paralel/seri bit esitligi.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/jobs/job_system.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "sim/animation.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
constexpr uint32_t J = 8, S = 60;

struct Fixture {
  SystemArena sys;
  Joint joints[J];
  Vec3 t[J][S];
  Quat r[J][S];
  Vec3 s[J][S];
  RawTrack tracks[J];
  RawClip raw;
  const ClipHeader *clip = nullptr;
  ClipBuildStats st;

  bool init() {
    if (!sys.reserve(16u << 20, "anim")) return false;
    for (uint32_t j = 0; j < J; j++) {
      joints[j].parent = j == 0 ? -1 : (int16_t)(j - 1);
      for (uint32_t k = 0; k < S; k++) {
        float ph = (float)k / S * 2 * kPi;
        // Kok: sabit ceviri; digerleri: 1 birim x'te (zincir), 3. eklem asagi yukari
        t[j][k] = j == 0 ? Vec3{0, 1, 0} : Vec3{1, j == 3 ? 0.25f * std::sin(ph) : 0.0f, 0};
        // Donus: her eklem farkli eksende sallanir; eklem 5 sabit
        float ang = j == 5 ? 0.3f : 0.5f * std::sin(ph + j);
        Vec3 axis = j % 3 == 0 ? Vec3{0, 1, 0} : j % 3 == 1 ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
        r[j][k] = Quat::axis_angle(axis, ang);
        s[j][k] = Vec3{1, 1, 1};
      }
      tracks[j] = RawTrack{t[j], r[j], s[j]};
    }
    raw = RawClip{tracks, J, S, 30.0f};
    clip = ClipBuilder::build(sys, raw, 1e-5f, &st);
    return clip != nullptr;
  }
};
Fixture *fx() {
  static Fixture f;
  static bool ok = f.init();
  return ok ? &f : nullptr;
}
float rot_err_deg(Quat a, Quat b) {
  float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  if (d > 1) d = 1;
  return 2.0f * std::acos(d) * 180.0f / kPi;
}
} // namespace

ENGINE_TEST(anim_compression_bounds_and_constant_elimination) {
  Fixture *f = fx();
  CHECK(f != nullptr);
  if (!f) return;
  // Sabit izler: kok ceviri (1) + eklem 1,2,4,5,6,7 ceviri (6) + eklem 5 donus (1) + tum olcekler (8) = 16
  CHECK(f->st.const_tracks == 16);
  CHECK(f->st.max_rot_error_deg < 0.05f);
  CHECK(f->st.max_pos_error < 0.001f);
  CHECK(f->st.compressed_bytes * 3 < f->st.raw_bytes);
  std::printf("    [bilgi] ham %zu B -> sikistirilmis %zu B (%.1fx); sabit iz %u/24; hata donus %.4f°, ceviri %.5f\n",
              f->st.raw_bytes, f->st.compressed_bytes, (double)f->st.raw_bytes / f->st.compressed_bytes,
              f->st.const_tracks, f->st.max_rot_error_deg, f->st.max_pos_error);
}

ENGINE_TEST(anim_sample_matches_raw_and_loops) {
  Fixture *f = fx();
  CHECK(f != nullptr);
  if (!f) return;
  Vec3 t[J], s[J];
  Quat r[J];
  LocalPose pose{t, r, s, J};
  ClipSampler::sample(f->clip, 0.0f, pose);
  bool ok = true;
  for (uint32_t j = 0; j < J; j++) {
    ok = ok && nearly_equal(t[j], f->t[j][0], 1e-3f) && rot_err_deg(r[j], f->r[j][0]) < 0.05f;
  }
  CHECK(ok);
  // Kare 10 = 10/30 s
  ClipSampler::sample(f->clip, 10.0f / 30.0f, pose);
  CHECK(nearly_equal(t[3], f->t[3][10], 1e-3f));
  // Yarim kare: iki komsunun arasinda
  ClipSampler::sample(f->clip, 10.5f / 30.0f, pose);
  float lo = f->t[3][10].y < f->t[3][11].y ? f->t[3][10].y : f->t[3][11].y;
  float hi = f->t[3][10].y < f->t[3][11].y ? f->t[3][11].y : f->t[3][10].y;
  CHECK(t[3].y >= lo - 1e-3f && t[3].y <= hi + 1e-3f);
  // Dongu: sure sonu = baslangic
  Vec3 t0[J], s0[J];
  Quat r0[J];
  LocalPose p0{t0, r0, s0, J};
  ClipSampler::sample(f->clip, 0.0f, p0);
  ClipSampler::sample(f->clip, f->clip->duration_s, pose);
  CHECK(std::memcmp(t, t0, sizeof t) == 0 && std::memcmp(r, r0, sizeof r) == 0);
}

ENGINE_TEST(anim_to_model_chains_parents) {
  Joint js[3];
  js[0].parent = -1; js[1].parent = 0; js[2].parent = 1;
  Skeleton sk{js, 3};
  Vec3 t[3] = {{0, 0, 0}, {1, 0, 0}, {1, 0, 0}};
  Quat r[3] = {Quat::axis_angle({0, 1, 0}, kPi * 0.5f), Quat::identity(), Quat::identity()};
  Vec3 s[3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
  LocalPose pose{t, r, s, 3};
  Mat4 m[3];
  ClipSampler::to_model(sk, pose, m);
  // Kok 90° y donusu: cocuklar x ekseninde 1,2 -> dunyada z ekseninde -1,-2
  CHECK(nearly_equal(transform_point(m[1], {0, 0, 0}), {0, 0, -1}, 1e-4f));
  CHECK(nearly_equal(transform_point(m[2], {0, 0, 0}), {0, 0, -2}, 1e-4f));
}

ENGINE_TEST(anim_sampling_allocates_nothing) {
  Fixture *f = fx();
  CHECK(f != nullptr);
  if (!f) return;
  Vec3 t[J], s[J];
  Quat r[J];
  LocalPose pose{t, r, s, J};
  Skeleton sk{f->joints, J};
  Mat4 m[J];
  AllocGate::begin_frame();
  for (int i = 0; i < 1000; i++) {
    ClipSampler::sample(f->clip, i * 0.0137f, pose);
    ClipSampler::to_model(sk, pose, m);
  }
  CHECK(AllocGate::end_frame() == 0);
}

namespace {
struct Inst {
  const ClipHeader *clip;
  float time;
  Vec3 t[J], s[J];
  Quat r[J];
};
void eval_job(void *p) {
  Inst *in = static_cast<Inst *>(p);
  LocalPose pose{in->t, in->r, in->s, J};
  ClipSampler::sample(in->clip, in->time, pose);
}
} // namespace

ENGINE_TEST(anim_parallel_eval_equals_serial) {
  Fixture *f = fx();
  CHECK(f != nullptr);
  if (!f) return;
  static SystemArena jsys;
  CHECK(jsys.reserve(8u << 20, "anim-js"));
  JobSystem js;
  CHECK(js.init(jsys, JobSystemConfig{}));
  static Inst par[64], ser[64];
  JobDecl decls[64];
  for (int i = 0; i < 64; i++) {
    par[i] = Inst{f->clip, i * 0.031f, {}, {}, {}};
    ser[i] = par[i];
    decls[i] = JobDecl{eval_job, &par[i], "anim_eval"};
  }
  Counter c;
  js.run(decls, 64, &c);
  js.wait(c);
  for (int i = 0; i < 64; i++) eval_job(&ser[i]);
  bool same = true;
  for (int i = 0; i < 64; i++)
    same = same && std::memcmp(par[i].t, ser[i].t, sizeof par[i].t) == 0 &&
           std::memcmp(par[i].r, ser[i].r, sizeof par[i].r) == 0;
  CHECK(same);
  js.shutdown();
}
