#version 450
// Toplamali yukari ornekleme: alt (daha kucuk, daha yayilmis) seviye 3x3 cadir
// suzgeciyle buyutulur ve ayni seviyedeki keskin zincire EKLENIR. Zincir boyu
// toplandigi icin yayilim genis ve yumusak olur (tek buyuk blur yerine).
layout(location = 0) in vec2 v_uv;
layout(set = 0, binding = 0) uniform sampler2D u_src;  // ayni seviye (keskin)
layout(set = 0, binding = 1) uniform sampler2D u_src2; // alt seviye (yayilan)
layout(push_constant) uniform Push {
  vec4 texel; // xy: 1/kaynak0, zw: 1/kaynak1 (alt seviye)
  vec4 p;     // z: yaricap carpani
} pc;
layout(location = 0) out vec4 o_color;
vec3 tent(vec2 uv, vec2 o) {
  vec3 s = texture(u_src2, uv + vec2(-o.x, -o.y)).rgb;
  s += texture(u_src2, uv + vec2(0.0, -o.y)).rgb * 2.0;
  s += texture(u_src2, uv + vec2(o.x, -o.y)).rgb;
  s += texture(u_src2, uv + vec2(-o.x, 0.0)).rgb * 2.0;
  s += texture(u_src2, uv).rgb * 4.0;
  s += texture(u_src2, uv + vec2(o.x, 0.0)).rgb * 2.0;
  s += texture(u_src2, uv + vec2(-o.x, o.y)).rgb;
  s += texture(u_src2, uv + vec2(0.0, o.y)).rgb * 2.0;
  s += texture(u_src2, uv + vec2(o.x, o.y)).rgb;
  return s * (1.0 / 16.0);
}
void main() {
  vec3 lo = tent(v_uv, pc.texel.zw * max(pc.p.z, 0.0));
  vec3 hi = texture(u_src, v_uv).rgb;
  o_color = vec4(hi + lo, 1.0);
}
