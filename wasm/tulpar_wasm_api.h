#ifndef TULPAR_WASM_API_H
#define TULPAR_WASM_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WebAssembly modülünü başlat
 * @return 0 başarılı, -1 hata
 */
int tulpar_wasm_init(void);

/**
 * Tulpar kodunu çalıştır
 * @param code Tulpar kaynak kodu
 * @return 0 başarılı, -1 derleme hatası, -2 runtime hatası
 */
int tulpar_wasm_run_code(const char *code);

/**
 * Output buffer'ı al
 * @return Output string
 */
const char *tulpar_wasm_get_output(void);

/**
 * Output buffer uzunluğunu al
 */
size_t tulpar_wasm_get_output_length(void);

/**
 * Temizlik yap
 */
void tulpar_wasm_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // TULPAR_WASM_API_H
