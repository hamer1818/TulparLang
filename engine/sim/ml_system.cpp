#include "sim/ml_system.hpp"

namespace tulpar::engine::sim {

bool ml_infer(const MlApproximator &approx, const float *input, float *output, float *scratch,
              uint32_t scratch_capacity) {
  if (!approx.valid() || !input || !output) return false;
  const uint32_t produced = approx.net->forward(input, output, scratch, scratch_capacity);
  return produced == approx.output_size() && produced > 0;
}

bool ml_predict_delta(const MlApproximator &approx, Vec3 pos, Vec3 vel, Vec3 *out_delta, float *scratch,
                      uint32_t scratch_capacity) {
  if (!out_delta) return false;
  // Boyut SOZLESMESI: yanlis boyutlu bir ag sessizce "calisiyor" gibi
  // gorunup anlamsiz sayi uretmesin.
  if (approx.input_size() != 6 || approx.output_size() != 3) return false;

  const float in[6] = {pos.x, pos.y, pos.z, vel.x, vel.y, vel.z};
  float out[3] = {0, 0, 0};
  if (!ml_infer(approx, in, out, scratch, scratch_capacity)) return false;
  *out_delta = Vec3{out[0], out[1], out[2]};
  return true;
}

} // namespace tulpar::engine::sim
