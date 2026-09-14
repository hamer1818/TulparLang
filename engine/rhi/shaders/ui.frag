#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(set = 1, binding = 0) uniform sampler2D u_atlas; // glif atlasi (beyaz + alfa); duz kutu icin beyaz texel
layout(location = 0) out vec4 o_color;
void main() { o_color = v_color * texture(u_atlas, v_uv); }
