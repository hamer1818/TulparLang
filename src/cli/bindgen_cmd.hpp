// `tulpar bindgen <baslik.h>` — C basligindan yerel eklenti bildirimi
// (tulpar-ext.json) uretir (FFI v1, 2026-11-26).
//
//   tulpar bindgen zlib.h --name zlib --lib z -o ext/tulpar-ext.json
//   tulpar bindgen SDL_timer.h --prefix SDL_ --lib SDL3
//
// libclang gerektirmez: on islemci satirlarini atan, yorumlari soyan, ust
// duzey bildirimleri `;` ile bolen kucuk bir C bildirim ayristiricisi.
// Desteklenen: skaler tipler, `T *` -> ptr, `const char *` -> str, enum ->
// i32, typedef zincirleri (skaler, struct, fonksiyon isaretcisi), fonksiyon
// isaretcisi parametresi -> `fn(...)`. Desteklenmeyen her bildirim
// GEREKCESIYLE raporlanir (variadic, struct deger gecisi K304, `_Bool`
// donusu, bilinmeyen tip) — sessizce atlanmaz.
//
// Cikis: 0 bildirim yazildi, 1 hicbir fonksiyon baglanamadi, 2 kullanim/G-C.
#ifndef TULPAR_BINDGEN_CMD_HPP
#define TULPAR_BINDGEN_CMD_HPP

namespace tulpar {
int bindgen_cmd_main(int argc, char **argv);
}

#endif
