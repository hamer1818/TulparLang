#version 450
// Bloom asama 1/2: kutu-filtreli KUCULTME (downsample). Mip zinciri
// yaklasimi (VIZYON.md'nin secimi: "Mipmap-based bloom (%52 daha hizli)" —
// 500 madde listesi #77 "Bloom (Mipmap-based)"). Her mip seviyesi bir onceki
// seviyeden 4-dokunuslu (2x2 kutu) ortalama ile yariya kucultulur; bu shader
// ZINCIRDE HER ADIM icin AYNI sekilde tekrar tekrar kosturulur (mip0->mip1->
// mip2->...), C++ tarafinda her seviye icin AYRI bir framebuffer/gecis GEREKIR.
//
// DERLEME/KABLOLA: bu YENI bir dosya (_spv.h yok, glslc gerekir).
// rhi/shaders/fullscreen.vert ile PAIR OLUR. Kablolamak icin:
// (1) renk hedefiyle AYNI formatta, YARI COZUNURLUKTE (ve devaminda ceyrek,
//     sekizde-bir...) bir dizi doku/framebuffer olustur (renderer.hpp'ye
//     "bloom_mips_[N]" gibi bir dizi eklenir),
// (2) her seviye icin bu shader'i BIR ONCEKI (daha buyuk) seviyeyi
//     okuyacak sekilde calistir (fullscreen.vert + bu frag, N-1 kez),
// (3) mesh.frag'in pozlama/tonemap adimindan ONCE, en kucuk mipten
//     baslayarak bloom_upsample.frag ile geri BUYUT ve TOPLA,
// (4) sonucu sahne rengine ekle (Renderer::set_bloom_intensity gibi yeni
//     bir cagriyla olcekli).
layout(location = 0) in vec2 v_uv;
layout(set = 1, binding = 0) uniform sampler2D u_src;
layout(push_constant) uniform Push {
  vec4 texel_size; // xy: 1/kaynak_genislik, 1/kaynak_yukseklik (KAYNAK mip'in, hedefin DEGIL)
} pc;
layout(location = 0) out vec4 o_color;

void main() {
  // 2x2 kutu filtresi: merkez etrafinda DORT ornek, esit agirlikli ortalama
  // -- her biri KAYNAK'in yarim-texel'i kadar kaydirilmis, tam olarak
  // "iki kaynak texel'in ortasi" konumunu orneklerler (dogru 2x downsample).
  // 13-dokunuslu (Jimenez/COD-tarzi) filtre daha az "parlama" (flicker)
  // verir ama burada BILEREK basit tutuldu -- ilk dilim, sonraki optimizasyon.
  vec2 o = pc.texel_size.xy * 0.5;
  vec3 c00 = texture(u_src, v_uv + vec2(-o.x, -o.y)).rgb;
  vec3 c10 = texture(u_src, v_uv + vec2( o.x, -o.y)).rgb;
  vec3 c01 = texture(u_src, v_uv + vec2(-o.x,  o.y)).rgb;
  vec3 c11 = texture(u_src, v_uv + vec2( o.x,  o.y)).rgb;
  o_color = vec4((c00 + c10 + c01 + c11) * 0.25, 1.0);
}
