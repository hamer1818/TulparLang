#include "renderer/nextgen_render.hpp"

#include <MaskedOcclusionCulling.h>

namespace tulpar::engine::renderer {

namespace {
MaskedOcclusionCulling *as_moc(void *p) { return static_cast<MaskedOcclusionCulling *>(p); }

OcclusionResult from_moc(MaskedOcclusionCulling::CullingResult r) {
  switch (r) {
    case MaskedOcclusionCulling::VISIBLE: return OcclusionResult::kVisible;
    case MaskedOcclusionCulling::OCCLUDED: return OcclusionResult::kOccluded;
    default: return OcclusionResult::kViewCulled;
  }
}
} // namespace

OcclusionCuller::~OcclusionCuller() { shutdown(); }

bool OcclusionCuller::init(uint32_t width, uint32_t height) {
  shutdown();
  // Kutuphanenin KENDI kurali (MaskedOcclusionCulling.h SetResolution
  // belgesinde): genislik 8'in, yukseklik 4'un kati olmali.
  if (width == 0 || height == 0 || (width % 8) != 0 || (height % 4) != 0) return false;

  // Create(): calisma zamaninda EN IYI desteklenen SIMD yolunu secer
  // (AVX512 -> AVX2 -> SSE4.1 -> SSE2); istenen seviye yoksa kendisi duser.
  MaskedOcclusionCulling *moc = MaskedOcclusionCulling::Create();
  if (!moc) return false;
  moc->SetResolution(width, height);
  moc_ = moc;
  width_ = width;
  height_ = height;
  return true;
}

void OcclusionCuller::shutdown() {
  if (moc_) {
    MaskedOcclusionCulling::Destroy(as_moc(moc_));
    moc_ = nullptr;
  }
  width_ = height_ = 0;
}

void OcclusionCuller::begin_frame() {
  if (moc_) as_moc(moc_)->ClearBuffer();
}

void OcclusionCuller::render_occluders(const float *vertices, const uint32_t *indices, uint32_t triangle_count,
                                       const float *model_to_clip) {
  if (!moc_ || !vertices || !indices || triangle_count == 0) return;
  as_moc(moc_)->RenderTriangles(vertices, indices, (int)triangle_count, model_to_clip);
}

OcclusionResult OcclusionCuller::test_rect(float xmin, float ymin, float xmax, float ymax, float w_min) const {
  if (!moc_) return OcclusionResult::kVisible; // kirpici yoksa HICBIR SEY elenmez (guvenli taraf)
  return from_moc(as_moc(moc_)->TestRect(xmin, ymin, xmax, ymax, w_min));
}

OcclusionResult OcclusionCuller::test_aabb(const Mat4 &viewproj, Aabb box) const {
  if (!moc_) return OcclusionResult::kVisible;

  float xmin = 1e30f, ymin = 1e30f, xmax = -1e30f, ymax = -1e30f, wmin = 1e30f;
  for (int i = 0; i < 8; i++) {
    const Vec3 corner{(i & 1) ? box.max.x : box.min.x,
                      (i & 2) ? box.max.y : box.min.y,
                      (i & 4) ? box.max.z : box.min.z};
    // Homojen donusum: core/math/vec.hpp'nin ZATEN test edilmis Mat4*Vec4
    // operatoru (kendi matris duzeni turetilmez).
    const Vec4 clip = viewproj * Vec4{corner.x, corner.y, corner.z, 1.0f};

    // Kose YAKIN DUZLEMIN arkasinda (w<=0): bolme anlamsiz, kutu kamerayi
    // sariyor olabilir -> GUVENLI taraf, elemeden gec.
    if (clip.w <= 1e-6f) return OcclusionResult::kVisible;

    const float inv_w = 1.0f / clip.w;
    const float ndc_x = clip.x * inv_w, ndc_y = clip.y * inv_w;
    const float w = clip.w;
    if (ndc_x < xmin) xmin = ndc_x;
    if (ndc_x > xmax) xmax = ndc_x;
    if (ndc_y < ymin) ymin = ndc_y;
    if (ndc_y > ymax) ymax = ndc_y;
    if (w < wmin) wmin = w;
  }
  return test_rect(xmin, ymin, xmax, ymax, wmin);
}

// --- Sanal doku sayfa tablosu ------------------------------------------

bool VirtualTexturePageTable::init(uint32_t page_count, uint16_t slot_count, uint16_t *page_to_slot,
                                   uint32_t *slot_to_page, uint64_t *slot_last_used) {
  if (page_count == 0 || slot_count == 0 || !page_to_slot || !slot_to_page || !slot_last_used) return false;
  page_to_slot_ = page_to_slot;
  slot_to_page_ = slot_to_page;
  slot_last_used_ = slot_last_used;
  page_count_ = page_count;
  slot_count_ = slot_count;
  resident_count_ = 0;
  tick_ = 0;
  for (uint32_t i = 0; i < page_count; i++) page_to_slot_[i] = kVtInvalidSlot;
  for (uint16_t s = 0; s < slot_count; s++) {
    slot_to_page_[s] = UINT32_MAX; // bos yuva
    slot_last_used_[s] = 0;
  }
  return true;
}

VtPageRequest VirtualTexturePageTable::request(uint32_t page_id) {
  VtPageRequest out;
  out.page_id = page_id;
  if (!page_to_slot_ || page_id >= page_count_) return out; // gecersiz istek: slot=kVtInvalidSlot

  tick_++;

  // (a) Zaten resident: yalniz LRU damgasini tazele.
  const uint16_t existing = page_to_slot_[page_id];
  if (existing != kVtInvalidSlot) {
    slot_last_used_[existing] = tick_;
    out.slot = existing;
    out.newly_loaded = false;
    return out;
  }

  // (b) Bos yuva ara (ilk tur -- onbellek daha dolmamis).
  for (uint16_t s = 0; s < slot_count_; s++) {
    if (slot_to_page_[s] == UINT32_MAX) {
      slot_to_page_[s] = page_id;
      page_to_slot_[page_id] = s;
      slot_last_used_[s] = tick_;
      resident_count_++;
      out.slot = s;
      out.newly_loaded = true;
      return out;
    }
  }

  // (c) Dolu: EN ESKI kullanilan yuvayi (LRU) tahliye et.
  uint16_t victim = 0;
  uint64_t oldest = slot_last_used_[0];
  for (uint16_t s = 1; s < slot_count_; s++) {
    if (slot_last_used_[s] < oldest) {
      oldest = slot_last_used_[s];
      victim = s;
    }
  }
  const uint32_t evicted = slot_to_page_[victim];
  if (evicted != UINT32_MAX && evicted < page_count_) page_to_slot_[evicted] = kVtInvalidSlot;
  slot_to_page_[victim] = page_id;
  page_to_slot_[page_id] = victim;
  slot_last_used_[victim] = tick_;
  out.slot = victim;
  out.evicted_page = evicted;
  out.newly_loaded = true;
  return out;
}

uint16_t VirtualTexturePageTable::slot_of(uint32_t page_id) const {
  if (!page_to_slot_ || page_id >= page_count_) return kVtInvalidSlot;
  return page_to_slot_[page_id];
}

} // namespace tulpar::engine::renderer
