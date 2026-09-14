#version 450
layout(location = 0) in vec3 v_nrm;
layout(location = 1) in vec3 v_color;
layout(location = 2) in vec4 v_light_pos;
layout(location = 3) in vec2 v_uv;
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 light_viewproj;
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params; // x: 1/boyut, y: sabit egilim, z: golge acik mi, w: normal kaydirma (dunya)
} u;
layout(set = 0, binding = 1) uniform sampler2DShadow u_shadow;
layout(set = 1, binding = 0) uniform sampler2D u_albedo; // malzeme (klasik set, bindless yok)
layout(location = 0) out vec4 o_color;

// 3x3 PCF; donanim karsilastirmali ornekleme (compareOp LESS_OR_EQUAL):
// texture() 1.0 = isikli. Harita disi = isikli (golge kutusu sahneyi kapsamali).
float shadow_visibility(float nl) {
  if (u.shadow_params.z < 0.5) return 1.0;
  vec3 p = v_light_pos.xyz / v_light_pos.w;
  if (p.z <= 0.0 || p.z >= 1.0) return 1.0;
  vec2 uv = p.xy * 0.5 + 0.5;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
  // Egik yuzeyde akne buyur: egilim normal-isik acisiyla olceklenir.
  float bias = u.shadow_params.y * clamp(1.0 - nl, 0.15, 1.0);
  float texel = u.shadow_params.x;
  float s = 0.0;
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++)
      s += texture(u_shadow, vec3(uv + vec2(float(x), float(y)) * texel, p.z - bias));
  return s / 9.0;
}

void main() {
  vec3 n = normalize(v_nrm);
  float nl = max(dot(n, normalize(u.light_dir.xyz)), 0.0);
  float vis = shadow_visibility(nl);
  vec3 albedo = texture(u_albedo, v_uv).rgb * v_color;
  vec3 c = albedo * (u.ambient.rgb + nl * vis * u.ambient.a);
  o_color = vec4(c, 1.0);
}
