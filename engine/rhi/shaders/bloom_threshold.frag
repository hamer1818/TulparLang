#version 450
// Bloom asama 0/2: esik (prefilter) gecisi -- mip zincirine girmeden ONCE
// yalniz PARLAK pikselleri gecirir (aksi halde TUM sahne bulanip "genel
// yumusatma" gibi gorunur, gercek bloom degil). bloom_downsample.frag'in
// mip0 girisidir (500 madde listesi #77 "Bloom (Mipmap-based)").
//
// Sert esik yerine DOGRUSAL yumusak-diz (soft-knee): parlaklik esikte ANI
// kesilmez, [threshold, threshold+knee] araliginda 0->1 DOGRUSAL geçiş yapar
// -- flicker/banding azaltir. Bu, belirli bir motorun (Unity/Unreal) ezbere
// hatirlanan sabit formulu DEGIL, kendi basit turetimi: contribution=0
// brightness<=threshold'da, contribution=1 brightness>=threshold+knee'de,
// arada dogrusal -- elle kanitlanabilir monoton bir rampa.
//
// DERLEME/KABLOLA: bu YENI bir dosya (_spv.h yok, glslc gerekir).
// rhi/shaders/fullscreen.vert ile PAIR OLUR. Girisi SAHNE renk hedefi
// (mesh.frag'in tonemap/pozlamadan ONCEKI DOGRUSAL HDR ciktisi -- bloom
// HER ZAMAN tonemap'ten ONCE, dogrusal uzayda hesaplanir, aksi halde
// yanlis parlaklik degerleri esiklenir).
layout(location = 0) in vec2 v_uv;
layout(set = 1, binding = 0) uniform sampler2D u_scene;
layout(push_constant) uniform Push {
  vec4 params; // x: threshold, y: knee (yumusak gecis genisligi), z/w: rezerve
} pc;
layout(location = 0) out vec4 o_color;

void main() {
  vec3 color = texture(u_scene, v_uv).rgb;
  const float threshold = pc.params.x;
  const float knee = max(pc.params.y, 1e-5);
  const float brightness = max(color.r, max(color.g, color.b));
  const float contribution = clamp((brightness - threshold) / knee, 0.0, 1.0);
  o_color = vec4(color * contribution, 1.0);
}
