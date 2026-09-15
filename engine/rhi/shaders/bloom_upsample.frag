#version 450
// Bloom asama 2/2: tent-filtreli BUYUTME (upsample) + TOPLAMA. Standart 3x3
// tent cekirdegi: agirliklar [1,2,1; 2,4,2; 1,2,1]/16 -- iki 1B tent
// cekirdeginin ([1,2,1]/4) DIS CARPIMI (1*1,1*2,1*1 / 2*1,2*2,2*1 / 1*1,1*2,1*1
// = 1,2,1/2,4,2/1,2,1, toplam 16) -- elle turetilebilir, ezbere bir sabit
// DEGIL. Mip zincirinin EN KUCUK seviyesinden baslayip yukari dogru
// TEKRARLANARAK cagirilir (bloom_downsample.frag ile ayni zincirin tersi;
// 500 madde listesi #77 "Bloom (Mipmap-based)").
//
// DERLEME/KABLOLA: bloom_downsample.frag ile AYNI durumda (KAYNAK yok,
// glslc gerekir). rhi/shaders/fullscreen.vert ile PAIR OLUR. Iki doku okur:
// u_small (bir ONCEKI/kucuk mip, BUYUTULECEK) ve u_current (bu seviyenin
// KENDI, downsample'dan kalma rengi -- USTUNE eklenecek). Boylece pipeline'da
// AYRI bir katma (additive blend) durumu KURULMASINA gerek kalmaz, shader
// kendi icinde toplar -- bkz. bloom_downsample.frag'daki genel kablolama notu.
layout(location = 0) in vec2 v_uv;
layout(set = 1, binding = 0) uniform sampler2D u_small;
layout(set = 1, binding = 1) uniform sampler2D u_current;
layout(push_constant) uniform Push {
  vec4 texel_size; // xy: 1/u_small_genislik, 1/u_small_yukseklik (BUYUTULEN dokunun, hedefin DEGIL)
} pc;
layout(location = 0) out vec4 o_color;

void main() {
  vec2 o = pc.texel_size.xy;
  vec3 sum = texture(u_small, v_uv + vec2(-o.x, -o.y)).rgb * 1.0
           + texture(u_small, v_uv + vec2( 0.0, -o.y)).rgb * 2.0
           + texture(u_small, v_uv + vec2( o.x, -o.y)).rgb * 1.0
           + texture(u_small, v_uv + vec2(-o.x,  0.0)).rgb * 2.0
           + texture(u_small, v_uv                    ).rgb * 4.0
           + texture(u_small, v_uv + vec2( o.x,  0.0)).rgb * 2.0
           + texture(u_small, v_uv + vec2(-o.x,  o.y)).rgb * 1.0
           + texture(u_small, v_uv + vec2( 0.0,  o.y)).rgb * 2.0
           + texture(u_small, v_uv + vec2( o.x,  o.y)).rgb * 1.0;
  vec3 blurred = sum * (1.0 / 16.0);
  vec3 current = texture(u_current, v_uv).rgb;
  o_color = vec4(blurred + current, 1.0);
}
