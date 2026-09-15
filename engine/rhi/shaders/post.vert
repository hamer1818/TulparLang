#version 450
// Tam ekran ucgeni (vertex tamponu YOK): 3 kose, gl_VertexIndex'ten uretilir.
// Dortgen yerine ucgen — kosegende iki kez ratlanan piksel olmaz (TBDR'de
// gereksiz fragment). Butun son-islem gecisleri bunu paylasir.
layout(location = 0) out vec2 v_uv;
void main() {
  vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  v_uv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
