#include "core/math/nn.hpp"

#include <cstddef> // size_t — libc++ (clang/macOS) gecisli getirmiyor, libstdc++ getiriyor
#include <cmath>

namespace tulpar::engine {

void dense_forward(const DenseLayer &layer, const float *input, float *output, Activation act) {
  for (uint32_t o = 0; o < layer.out_dim; o++) {
    float sum = layer.bias ? layer.bias[o] : 0.0f;
    const float *row = layer.weights + (size_t)o * layer.in_dim;
    for (uint32_t i = 0; i < layer.in_dim; i++) sum += row[i] * input[i];
    switch (act) {
      case Activation::kReLU:
        output[o] = sum > 0.0f ? sum : 0.0f;
        break;
      case Activation::kSigmoid:
        output[o] = 1.0f / (1.0f + std::exp(-sum));
        break;
      case Activation::kTanh:
        output[o] = std::tanh(sum);
        break;
      default:
        output[o] = sum;
        break;
    }
  }
}

uint32_t Sequential::forward(const float *input, float *output, float *scratch, uint32_t scratch_capacity) const {
  if (layer_count == 0) return 0;
  const uint32_t half = scratch_capacity / 2;
  float *buf_a = scratch;
  float *buf_b = scratch + half;
  const float *cur_in = input;
  float *cur_out = buf_a;
  for (uint32_t i = 0; i < layer_count; i++) {
    float *dst = (i + 1 == layer_count) ? output : cur_out;
    dense_forward(layers[i], cur_in, dst, activations[i]);
    cur_in = dst;
    cur_out = (cur_out == buf_a) ? buf_b : buf_a;
  }
  return layers[layer_count - 1].out_dim;
}

} // namespace tulpar::engine
