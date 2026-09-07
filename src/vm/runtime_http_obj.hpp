#ifndef TULPAR_RUNTIME_HTTP_OBJ_HPP
#define TULPAR_RUNTIME_HTTP_OBJ_HPP

// Arena'da JSON benzeri nesne kuran ucluk. `runtime_bindings.cpp`de
// tanimli, `runtime_net.cpp` de kullaniyor — ag yuzeyi ayri bir derleme
// birimine tasindiginda (OpenSSL'i her ikiliye baglamamak icin) bu uc
// yardimci paylasilir hale geldi.
#include "vm.hpp"

#ifdef __cplusplus
extern "C" {
#endif

ObjObject *aot_http_make_obj(int initial_capacity);
void aot_http_obj_set(ObjObject *o, const char *key, int key_len, VMValue val);
void aot_http_obj_set_str(ObjObject *o, const char *key, int klen,
                          const char *val, int vlen);

#ifdef __cplusplus
}
#endif

#endif
