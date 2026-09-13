#version 450
// Faz 1 ilk piksel: vertex buffer yok, uc kose gl_VertexIndex'ten.
// Vulkan NDC: y asagi; (0,-0.6) ust kose. z=0.5 -> depth prepass yazar.
layout(location = 0) out vec3 v_color;
void main() {
  const vec2 pos[3] = vec2[](vec2(-0.6, 0.6), vec2(0.6, 0.6), vec2(0.0, -0.6));
  gl_Position = vec4(pos[gl_VertexIndex], 0.5, 1.0);
  v_color = vec3(1.0, 0.5, 0.0);
}
