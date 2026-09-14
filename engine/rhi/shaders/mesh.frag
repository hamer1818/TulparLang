#version 450
layout(location = 0) in vec3 v_nrm;
layout(location = 1) in vec3 v_color;
layout(set = 0, binding = 0) uniform Frame { mat4 viewproj; vec4 light_dir; vec4 ambient; } u;
layout(location = 0) out vec4 o_color;
void main() {
  float nl = max(dot(normalize(v_nrm), normalize(u.light_dir.xyz)), 0.0);
  vec3 c = v_color * (u.ambient.rgb + nl * u.ambient.a);
  o_color = vec4(c, 1.0);
}
