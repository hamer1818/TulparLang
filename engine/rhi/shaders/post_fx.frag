#version 450
// Vinyet + kromatik sapma (chromatic aberration) + film tanesi (grain) --
// tek gecisli, ucuz (LUT/ek doku YOK, yalniz analitik) son-isleme bloğu.
// rhi/shaders/fullscreen.vert ile PAIR OLUR -- bloom_threshold.frag ile
// AYNI arayuz sozlesmesi (set 1 binding 0 giris dokusu, tek vec4 push
// constant), 500 madde listesindeki gorsel-kalite maddelerine (post-
// process zinciri) ek, kullanicinin acikca istedigi "gorsel/shader" yonu.
//
// Kromatik sapma + vinyet: merkezde etkisiz, kenara dogru KADEMELI --
// hicbir motora ozgu olmayan, standart ekran-uzayi post-fx tekniği (bkz.
// coğu modern oyunun "lens/camera fx" ayarlari -- Unity Post Processing
// Stack, Unreal Lens Effects, vb. HEPSI ayni temel fikri kullanir: RGB
// kanallarini merkezden uzaklikla orantili KAYDIR, kenari smoothstep ile
// karart).
//
// Film tanesi: interleaved gradient noise (Jorge Jimenez, SIGGRAPH 2014,
// "Next Generation Post Processing in Call of Duty: Advanced Warfare") --
// bu UC sabit (0.06711056, 0.00583715, 52.9829189) literatürde SABIT,
// tek-orneklem dithering/grain icin yaygin kullanilan, TAM olarak bilinen
// bir formul (rastgele uydurulmus degil).
//
// DERLEME/KABLOLA: bu YENI bir dosya, henuz derlenmis SPIR-V'si (_spv.h)
// YOK -- bu makinede glslc olmadigi icin uretilemedi, HICBIR C++ koduna
// #include EDILMEDI (derleme BOZULMAZ). Kablolamak icin bloom_threshold.frag
// basindaki NOT ile AYNI adimlar: compile_shaders.py -> pipeline -> draw
// cagrisinda push constant doldur.
layout(location = 0) in vec2 v_uv;
layout(set = 1, binding = 0) uniform sampler2D u_scene;
layout(push_constant) uniform Push {
  vec4 params; // x: vinyet gucu [0,1], y: sapma gucu (~0.001-0.004), z: tane gucu [0,~0.05], w: zaman(s)
} pc;
layout(location = 0) out vec4 o_color;

// Jorge Jimenez, SIGGRAPH 2014 -- interleaved gradient noise (IGN).
float interleaved_gradient_noise(vec2 pixel_co) {
  const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
  return fract(magic.z * fract(dot(pixel_co, magic.xy)));
}

void main() {
  const float vignette_strength = pc.params.x;
  const float aberration_strength = pc.params.y;
  const float grain_strength = pc.params.z;
  const float time_s = pc.params.w;

  const vec2 centered = v_uv - 0.5;
  const float dist = length(centered); // merkezde 0, kosede ~0.707

  // R/G/B kanallari merkeze gore FARKLI ekranuzayi ofsetiyle ornekle --
  // dist=0 (merkez) => ofset=0 (sapma YOK), kenara dogru KARE-orantili artar.
  const vec2 dir = centered * aberration_strength * dist;
  const float r = texture(u_scene, v_uv - dir).r;
  const float g = texture(u_scene, v_uv).g;
  const float b = texture(u_scene, v_uv + dir).b;
  vec3 color = vec3(r, g, b);

  // Vinyet: merkezde carpan 1.0 (degisiklik yok), [0.2,0.75] araliginda
  // DUZGUN (smoothstep) karariyor -- ani kenar YOK.
  const float vignette = 1.0 - vignette_strength * smoothstep(0.2, 0.75, dist);
  color *= vignette;

  // Film tanesi: piksel+zaman tohumlu IGN, [-1,1]'e tasinir, dusuk genlikle
  // eklenir -- zaman terimi OLMADAN tane sabit kalir (goze batan "yapiskan"
  // desen), time_s ile HER karede farkli tohum kullanilir.
  const vec2 pixel_co = gl_FragCoord.xy + vec2(time_s * 137.0, time_s * 91.0);
  const float grain = interleaved_gradient_noise(pixel_co) * 2.0 - 1.0;
  color += grain * grain_strength;

  o_color = vec4(color, 1.0);
}
