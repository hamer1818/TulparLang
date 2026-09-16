// core/math/nn.hpp: TEK KATMANLI cikisi elle carpim-toplam ile dogrudan
// hesaplanip dogrulanir (gizli bir algoritma yok -- asil risk indeksleme/
// ping-pong arabellek sirasidir, testler tam bunu hedefler). ReLU'nun
// TAM DETERMINISTIK oldugu (libm cagirmadigi) tasarim geregi -- ayni
// girdiyle iki BAGIMSIZ cagrinin BIT-ES sonuc verdigi ayrica dogrulanir.
#include "core/math/nn.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(dense_forward_matches_hand_computed_relu_output) {
  // out0 = 0.5 + 1*1 + 2*1 = 3.5 -> ReLU(3.5)=3.5
  // out1 = -10 + 3*1 + 4*1 = -3 -> ReLU(-3)=0
  const float weights[4] = {1, 2, 3, 4}; // satir-major: [1,2] (out0), [3,4] (out1)
  const float bias[2] = {0.5f, -10.0f};
  const float input[2] = {1, 1};
  float output[2];
  DenseLayer layer{weights, bias, 2, 2};
  dense_forward(layer, input, output, Activation::kReLU);
  CHECK(output[0] == 3.5f);
  CHECK(output[1] == 0.0f);
}

ENGINE_TEST(dense_forward_none_activation_is_pure_linear) {
  const float weights[2] = {2, -1};
  const float input[2] = {3, 4};
  float output[1];
  DenseLayer layer{weights, nullptr, 2, 1}; // bias YOK
  dense_forward(layer, input, output, Activation::kNone);
  CHECK(output[0] == 2.0f); // 2*3 + (-1)*4 = 2
}

ENGINE_TEST(dense_forward_is_bit_exact_deterministic_with_relu) {
  const float weights[6] = {0.3f, -0.7f, 1.1f, 0.2f, -0.9f, 0.4f};
  const float bias[2] = {0.1f, -0.2f};
  const float input[3] = {1.5f, -2.5f, 0.75f};
  float out1[2], out2[2];
  DenseLayer layer{weights, bias, 3, 2};
  dense_forward(layer, input, out1, Activation::kReLU);
  dense_forward(layer, input, out2, Activation::kReLU);
  CHECK(out1[0] == out2[0] && out1[1] == out2[1]); // TAM ayni bit dizisi
}

ENGINE_TEST(sequential_two_layer_chain_matches_hand_computed_result) {
  // Katman0: kimlik matrisi (2->2), ReLU -- girdiyi (pozitifse) degistirmez.
  // Katman1: iki girdiyi TOPLAR (2->1), aktivasyon yok.
  // Girdi [3,4] -> katman0 [3,4] (degismez) -> katman1 3+4=7.
  const float w0[4] = {1, 0, 0, 1};
  const float b0[2] = {0, 0};
  const float w1[2] = {1, 1};
  const float b1[1] = {0};

  Sequential seq;
  CHECK(seq.add_layer(DenseLayer{w0, b0, 2, 2}, Activation::kReLU));
  CHECK(seq.add_layer(DenseLayer{w1, b1, 2, 1}, Activation::kNone));

  const float input[2] = {3, 4};
  float output[1];
  float scratch[8]; // 2 * (en genis ara katman genisligi=2) icin bol miktarda yer
  uint32_t out_dim = seq.forward(input, output, scratch, 8);
  CHECK(out_dim == 1);
  CHECK(output[0] == 7.0f);
}

ENGINE_TEST(sigmoid_and_tanh_stay_within_expected_ranges) {
  // Bunlar libm KULLANIR (std::exp/std::tanh) -- platformlar arasi BIT-ES
  // GARANTI EDILMEZ, bu yuzden yalniz ARALIK ozelligi test edilir (tam
  // deger degil) -- ReLU testlerinden BILINCLI olarak farkli bir standart.
  // KAPALI aralik ([0,1], [-1,1]): asiri buyuk |v| degerlerinde exp()
  // TASAR (float overflow -> +inf), sigmoid TAM 0.0/1.0'a, tanh TAM
  // -1.0/1.0'a DOYAR -- bu bir HATA degil, kayan nokta doyma davranisi
  // (ACIK aralik varsayimi bu uc degerlerde YANLIS olurdu).
  const float w[1] = {1.0f};
  const float input_vals[5] = {-100.0f, -1.0f, 0.0f, 1.0f, 100.0f};
  for (float v : input_vals) {
    float out_sig, out_tanh;
    DenseLayer layer{w, nullptr, 1, 1};
    dense_forward(layer, &v, &out_sig, Activation::kSigmoid);
    dense_forward(layer, &v, &out_tanh, Activation::kTanh);
    CHECK(out_sig >= 0.0f && out_sig <= 1.0f);
    CHECK(out_tanh >= -1.0f && out_tanh <= 1.0f);
  }
}
