#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in float v_encode;
layout(set = 1, binding = 0) uniform sampler2D u_atlas; // glif atlasi (beyaz + alfa); duz kutu icin beyaz texel
layout(location = 0) out vec4 o_color;
// UI rengi sRGB algisal verilir; hedef dogrusal (SRGB bicim donanimda kodlar).
// Kaplama (atlas) UNORM'dur, oldugu gibi carpilir; karisim dogrusal uzayda.
vec3 srgb_to_linear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }
vec3 linear_to_srgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
void main() {
  vec4 c = vec4(srgb_to_linear(v_color.rgb), v_color.a) * texture(u_atlas, v_uv);
  if (v_encode > 0.5) c.rgb = linear_to_srgb(c.rgb);
  o_color = c;
}
