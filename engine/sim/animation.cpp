#include "sim/animation.hpp"

#include <cmath>
#include <cstring>

#include "platform/fatal.hpp"

namespace tulpar::engine::sim {

namespace {
constexpr uint32_t kMagic = 0x504C4354u; // 'TCLP'
constexpr float kInvSqrt2 = 0.70710678f;

// --- niceleme --------------------------------------------------------------
inline uint16_t q16(float v, float mn, float ext) { // [mn, mn+ext] -> 0..65535
  float n = ext > 0 ? (v - mn) / ext : 0.0f;
  if (n < 0) n = 0;
  if (n > 1) n = 1;
  return (uint16_t)(n * 65535.0f + 0.5f);
}
inline float dq16(uint16_t q, float mn, float ext) { return mn + ext * (q / 65535.0f); }

// Donus: en kucuk uc. Birakilan bilesen en buyuk |x|; isareti pozitif yapilir
// (q ve -q ayni donus). Uc bilesen [-1/sqrt2, 1/sqrt2] -> 15 bit; birakilan
// indeks (2 bit) ilk iki degerin 15. bitinde.
inline void enc_rot(Quat q, uint16_t out[3]) {
  float c[4] = {q.x, q.y, q.z, q.w};
  int big = 0;
  for (int i = 1; i < 4; i++) if (std::fabs(c[i]) > std::fabs(c[big])) big = i;
  if (c[big] < 0) for (float &v : c) v = -v;
  int k = 0;
  for (int i = 0; i < 4; i++) {
    if (i == big) continue;
    float n = (c[i] + kInvSqrt2) / (2 * kInvSqrt2);
    if (n < 0) n = 0;
    if (n > 1) n = 1;
    uint16_t v = (uint16_t)(n * 32767.0f + 0.5f);
    out[k++] = v;
  }
  out[0] |= (uint16_t)((big & 1) << 15);
  out[1] |= (uint16_t)(((big >> 1) & 1) << 15);
}
inline Quat dec_rot(const uint16_t in[3]) {
  int big = (in[0] >> 15) | ((in[1] >> 15) << 1);
  float c[4];
  int k = 0;
  float sum = 0;
  for (int i = 0; i < 4; i++) {
    if (i == big) continue;
    float n = (float)(in[k++] & 0x7FFF) / 32767.0f;
    c[i] = n * (2 * kInvSqrt2) - kInvSqrt2;
    sum += c[i] * c[i];
  }
  float w2 = 1.0f - sum;
  c[big] = w2 > 0 ? std::sqrt(w2) : 0.0f;
  return Quat{c[0], c[1], c[2], c[3]};
}

inline bool near3(Vec3 a, Vec3 b, float eps) {
  return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}
inline bool near_q(Quat a, Quat b, float eps) {
  float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  return d >= 1.0f - eps;
}
inline float rot_err_deg(Quat a, Quat b) {
  float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  if (d > 1) d = 1;
  return 2.0f * std::acos(d) * 180.0f / kPi;
}
} // namespace

const ClipHeader *ClipBuilder::build(Arena &arena, const RawClip &raw, float const_eps, ClipBuildStats *stats) {
  const uint32_t J = raw.joints, S = raw.samples;
  ENGINE_ASSERT(J > 0 && S > 0);
  // 1) sabit izler
  uint8_t *tc = arena.alloc_array_zeroed<uint8_t>(J), *rc = arena.alloc_array_zeroed<uint8_t>(J),
          *sc = arena.alloc_array_zeroed<uint8_t>(J);
  uint32_t const_floats = 0, anim_stride = 0, n_const = 0;
  for (uint32_t j = 0; j < J; j++) {
    const RawTrack &tr = raw.tracks[j];
    bool ct = true, cr = true, cs = true;
    for (uint32_t s = 1; s < S; s++) {
      if (ct && !near3(tr.t[s], tr.t[0], const_eps)) ct = false;
      if (cr && !near_q(tr.r[s], tr.r[0], 1e-6f)) cr = false;
      if (cs && tr.s && !near3(tr.s[s], tr.s[0], const_eps)) cs = false;
    }
    tc[j] = ct; rc[j] = cr; sc[j] = cs || tr.s == nullptr;
    const_floats += (ct ? 3 : 0) + (cr ? 4 : 0) + (sc[j] ? 3 : 0);
    anim_stride += (ct ? 0 : 3) + (cr ? 0 : 3) + (sc[j] ? 0 : 3);
    n_const += (uint32_t)ct + (uint32_t)cr + (uint32_t)sc[j];
  }
  // 2) yerlesim
  uint32_t desc_off = (uint32_t)align_up(sizeof(ClipHeader), 16);
  uint32_t const_off = (uint32_t)align_up(desc_off + sizeof(TrackDesc) * J, 16);
  uint32_t anim_off = (uint32_t)align_up(const_off + const_floats * sizeof(float), 16);
  uint32_t total = anim_off + anim_stride * S * (uint32_t)sizeof(uint16_t);
  uint8_t *blob = static_cast<uint8_t *>(arena.alloc(total, 16));
  if (!blob) return nullptr;
  std::memset(blob, 0, total);
  ClipHeader *h = reinterpret_cast<ClipHeader *>(blob);
  h->magic = kMagic; h->joints = J; h->samples = S; h->sample_rate = raw.sample_rate;
  h->duration_s = S / raw.sample_rate; h->track_desc_off = desc_off; h->const_off = const_off;
  h->anim_off = anim_off; h->total_bytes = total;
  TrackDesc *desc = reinterpret_cast<TrackDesc *>(blob + desc_off);
  float *consts = reinterpret_cast<float *>(blob + const_off);
  uint16_t *anim = reinterpret_cast<uint16_t *>(blob + anim_off);
  uint32_t cf = 0, comp = 0;
  for (uint32_t j = 0; j < J; j++) {
    const RawTrack &tr = raw.tracks[j];
    TrackDesc &d = desc[j];
    d.t_const = tc[j]; d.r_const = rc[j]; d.s_const = sc[j];
    if (tc[j]) { d.t_idx = cf; consts[cf++] = tr.t[0].x; consts[cf++] = tr.t[0].y; consts[cf++] = tr.t[0].z; }
    else {
      Vec3 mn = tr.t[0], mx = tr.t[0];
      for (uint32_t s = 1; s < S; s++) { mn = vmin(mn, tr.t[s]); mx = vmax(mx, tr.t[s]); }
      d.t_min[0] = mn.x; d.t_min[1] = mn.y; d.t_min[2] = mn.z;
      d.t_ext[0] = mx.x - mn.x; d.t_ext[1] = mx.y - mn.y; d.t_ext[2] = mx.z - mn.z;
      d.t_idx = comp; comp += 3;
    }
    if (rc[j]) { d.r_idx = cf; consts[cf++] = tr.r[0].x; consts[cf++] = tr.r[0].y; consts[cf++] = tr.r[0].z; consts[cf++] = tr.r[0].w; }
    else { d.r_idx = comp; comp += 3; }
    if (sc[j]) {
      Vec3 s0 = tr.s ? tr.s[0] : Vec3{1, 1, 1};
      d.s_idx = cf; consts[cf++] = s0.x; consts[cf++] = s0.y; consts[cf++] = s0.z;
    } else {
      Vec3 mn = tr.s[0], mx = tr.s[0];
      for (uint32_t s = 1; s < S; s++) { mn = vmin(mn, tr.s[s]); mx = vmax(mx, tr.s[s]); }
      d.s_min[0] = mn.x; d.s_min[1] = mn.y; d.s_min[2] = mn.z;
      d.s_ext[0] = mx.x - mn.x; d.s_ext[1] = mx.y - mn.y; d.s_ext[2] = mx.z - mn.z;
      d.s_idx = comp; comp += 3;
    }
  }
  ENGINE_ASSERT(comp == anim_stride);
  // 3) ornekleri nicele
  float max_rot = 0, max_pos = 0;
  for (uint32_t s = 0; s < S; s++) {
    uint16_t *row = anim + (size_t)s * anim_stride;
    for (uint32_t j = 0; j < J; j++) {
      const RawTrack &tr = raw.tracks[j];
      const TrackDesc &d = desc[j];
      if (!d.t_const) {
        for (int c = 0; c < 3; c++) {
          float v = c == 0 ? tr.t[s].x : c == 1 ? tr.t[s].y : tr.t[s].z;
          row[d.t_idx + c] = q16(v, d.t_min[c], d.t_ext[c]);
          float back = dq16(row[d.t_idx + c], d.t_min[c], d.t_ext[c]);
          if (std::fabs(back - v) > max_pos) max_pos = std::fabs(back - v);
        }
      }
      if (!d.r_const) {
        enc_rot(tr.r[s], &row[d.r_idx]);
        float e = rot_err_deg(dec_rot(&row[d.r_idx]), tr.r[s]);
        if (e > max_rot) max_rot = e;
      }
      if (!d.s_const) {
        for (int c = 0; c < 3; c++) {
          float v = c == 0 ? tr.s[s].x : c == 1 ? tr.s[s].y : tr.s[s].z;
          row[d.s_idx + c] = q16(v, d.s_min[c], d.s_ext[c]);
        }
      }
    }
  }
  if (stats) {
    stats->raw_bytes = (size_t)J * S * (3 + 4 + 3) * sizeof(float);
    stats->compressed_bytes = total;
    stats->const_tracks = n_const;
    stats->max_rot_error_deg = max_rot;
    stats->max_pos_error = max_pos;
  }
  return h;
}

void ClipSampler::sample(const ClipHeader *clip, float time_s, LocalPose &out) {
  ENGINE_ASSERT(clip && clip->magic == kMagic && out.joints == clip->joints);
  const uint8_t *blob = reinterpret_cast<const uint8_t *>(clip);
  const TrackDesc *desc = reinterpret_cast<const TrackDesc *>(blob + clip->track_desc_off);
  const float *consts = reinterpret_cast<const float *>(blob + clip->const_off);
  const uint16_t *anim = reinterpret_cast<const uint16_t *>(blob + clip->anim_off);
  const uint32_t S = clip->samples;
  // Dongusel kare konumu. Derleyici/libm'siz: fmodf yerine dogrudan.
  float f = time_s * clip->sample_rate;
  float wrap = (float)S;
  while (f >= wrap) f -= wrap;
  while (f < 0) f += wrap;
  uint32_t i0 = (uint32_t)f;
  if (i0 >= S) i0 = S - 1;
  uint32_t i1 = (i0 + 1 == S) ? 0 : i0 + 1;
  float a = f - (float)i0;
  uint32_t stride = 0;
  for (uint32_t j = 0; j < clip->joints; j++)
    stride += (desc[j].t_const ? 0 : 3) + (desc[j].r_const ? 0 : 3) + (desc[j].s_const ? 0 : 3);
  const uint16_t *r0 = anim + (size_t)i0 * stride, *r1 = anim + (size_t)i1 * stride;
  for (uint32_t j = 0; j < clip->joints; j++) {
    const TrackDesc &d = desc[j];
    if (d.t_const) out.t[j] = Vec3{consts[d.t_idx], consts[d.t_idx + 1], consts[d.t_idx + 2]};
    else {
      Vec3 p0{dq16(r0[d.t_idx], d.t_min[0], d.t_ext[0]), dq16(r0[d.t_idx + 1], d.t_min[1], d.t_ext[1]), dq16(r0[d.t_idx + 2], d.t_min[2], d.t_ext[2])};
      Vec3 p1{dq16(r1[d.t_idx], d.t_min[0], d.t_ext[0]), dq16(r1[d.t_idx + 1], d.t_min[1], d.t_ext[1]), dq16(r1[d.t_idx + 2], d.t_min[2], d.t_ext[2])};
      out.t[j] = lerp(p0, p1, a);
    }
    if (d.r_const) out.r[j] = Quat{consts[d.r_idx], consts[d.r_idx + 1], consts[d.r_idx + 2], consts[d.r_idx + 3]};
    else {
      Quat q0 = dec_rot(&r0[d.r_idx]), q1 = dec_rot(&r1[d.r_idx]);
      float dot = q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;
      if (dot < 0) q1 = Quat{-q1.x, -q1.y, -q1.z, -q1.w};
      out.r[j] = normalize(Quat{q0.x + (q1.x - q0.x) * a, q0.y + (q1.y - q0.y) * a, q0.z + (q1.z - q0.z) * a, q0.w + (q1.w - q0.w) * a});
    }
    if (d.s_const) out.s[j] = Vec3{consts[d.s_idx], consts[d.s_idx + 1], consts[d.s_idx + 2]};
    else {
      Vec3 s0{dq16(r0[d.s_idx], d.s_min[0], d.s_ext[0]), dq16(r0[d.s_idx + 1], d.s_min[1], d.s_ext[1]), dq16(r0[d.s_idx + 2], d.s_min[2], d.s_ext[2])};
      Vec3 s1{dq16(r1[d.s_idx], d.s_min[0], d.s_ext[0]), dq16(r1[d.s_idx + 1], d.s_min[1], d.s_ext[1]), dq16(r1[d.s_idx + 2], d.s_min[2], d.s_ext[2])};
      out.s[j] = lerp(s0, s1, a);
    }
  }
}

void ClipSampler::to_model(const Skeleton &sk, const LocalPose &local, Mat4 *out_model) {
  ENGINE_ASSERT(sk.count == local.joints);
  for (uint32_t j = 0; j < sk.count; j++) {
    Mat4 l = Mat4::translate(local.t[j]) * to_mat4(local.r[j]) * Mat4::scale(local.s[j]);
    int16_t p = sk.joints[j].parent;
    ENGINE_ASSERT(p < (int16_t)j); // ebeveyn once
    out_model[j] = p < 0 ? l : out_model[p] * l;
  }
}

} // namespace tulpar::engine::sim
