#version 450
// 2B arayuz: piksel uzayinda dortgenler (metin, kutu, joystick). Push sabiti
// (ana pipeline layout'u paylasilir, ilk 8 bayt): ekran olcusu.
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(push_constant) uniform Push { vec2 screen; vec2 rot; float encode; } pc; // rot = (cos, sin): Android on-dondurme; encode: UNORM hedefte shader kodlar
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) flat out float v_encode;
void main() {
  // MANTIKSAL (gorunen) piksel -> NDC, sonra on-dondurme ile ayni acida dondur
  // (3B projeksiyonla ayni: goruntu panelin dogal yonunde, kompozitor dondurmez).
  vec2 n = vec2(in_pos.x / pc.screen.x * 2.0 - 1.0, in_pos.y / pc.screen.y * 2.0 - 1.0);
  gl_Position = vec4(pc.rot.x * n.x - pc.rot.y * n.y, pc.rot.y * n.x + pc.rot.x * n.y, 0.0, 1.0);
  v_uv = in_uv;
  v_color = in_color;
  v_encode = pc.encode;
}
