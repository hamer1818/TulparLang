#version 450
// Tam-ekran ucgen (fullscreen triangle) — post-process gecisleri (bloom,
// tonemap-sonrasi kompozisyon, TAA resolve, vb.) icin standart, yaygin
// bilinen teknik: vertex buffer YOK, 3 kose gl_VertexIndex'ten bit
// hileleriyle uretilir; ekranin TAMAMINI (ve fazlasini, klip ile kirpilir)
// tek ucgenle kapsar — dortgen (2 ucgen) yerine ucgen kullanmanin nedeni
// diyagonal kenar boyunca fazladan interpolasyon/bant genisligi olmamasi.
//
// v_uv: (0,0) sol-ust -> (1,1) sag-alt (Vulkan doku/goruntu uzayi sozlesmesi,
// ekstra ters cevirme GEREKMEZ). Kose UV'leri [0,2] araligina tasar, klip
// SONRASI gorunen [0,1] bolgesi dogru kalir (bu, tekniğin standart parcasi).
//
// DERLEME/KABLOLA: bu YENI bir dosya (_spv.h yok, glslc gerekir). Herhangi
// bir post-process frag shader'la (bloom_downsample.frag, bloom_upsample.frag,
// vb.) PAIR OLUR — push constant/UBO gerektirmez, YALNIZ bir doku+sampler
// (set 1, binding 0) okur.
layout(location = 0) out vec2 v_uv;

void main() {
  vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  v_uv = uv;
  gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
