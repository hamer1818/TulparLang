#version 450
// Golge haritasi, iskeletli: shadow.vert + 4 eklem karisimi (ayni SSBO, ayni push).
layout(location = 0) in vec3 in_pos;
layout(location = 3) in uvec4 in_joints;
layout(location = 4) in vec4 in_weights;
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewproj;
  mat4 view;
  mat4 light_viewproj[3]; // golge kademeleri (Renderer::kMaxCascades)
  vec4 light_dir;
  vec4 ambient;
  vec4 shadow_params;
  vec4 cluster_params;
  uvec4 cluster_grid;
  vec4 cascade_params; // x: kademe sayisi, y: 1/kademe, z: atlas texel x, w: atlas texel y
} u;
layout(std430, set = 0, binding = 4) readonly buffer Skin { mat4 m[]; } u_skin;
layout(push_constant) uniform Push { mat4 model; vec4 color; uvec4 skin; } pc;
void main() {
  mat4 skin = in_weights.x * u_skin.m[pc.skin.x + in_joints.x] + in_weights.y * u_skin.m[pc.skin.x + in_joints.y] +
              in_weights.z * u_skin.m[pc.skin.x + in_joints.z] + in_weights.w * u_skin.m[pc.skin.x + in_joints.w];
  gl_Position = u.light_viewproj[pc.skin.y] * pc.model * skin * vec4(in_pos, 1.0);
}
