#version 450
// UI OVERDRAW SAYIMI (olcum boru hatti; normal karede KULLANILMAZ).
//
// Her rasterlanan fragment hedefe +1/255 EKLER (toplamali harmanlama, yalniz R
// kanali; hedef UNORM8 oldugu icin 1/255 TAM temsil edilir, yani toplam tamsayi
// kalir). Geri okunan R bayti o pikseldeki fragment sayisidir: tahmini alan
// toplami degil, RASTERLAYICININ gercek sayimi. Doku ORNEKLENMEZ — alfasi 0
// olan bir glif pikseli de tile belleginde okunup yazilir, yani overdraw'dir.
//
// Vertex shader'in cikislarini TUKETMEZ (glslc -O zaten eler). Bu yuzden bu
// boru hatti TEMBEL yaratilir: ui_record_overdraw cagrilmadikca hic kurulmaz,
// boylece Mali/BestPractices kapisi "kullanilmayan cikti" uyarisi gormez.
layout(location = 0) out vec4 o_color;
void main() { o_color = vec4(1.0 / 255.0, 0.0, 0.0, 0.0); }
