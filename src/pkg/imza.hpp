// PAKET IMZALAMA (2026-11-26) — Ed25519, ayrik imza (`<dosya>.sig`).
//
// Neden: tulpar.lock'taki sha256 "ilk indirdigimle ayni bayt" der; ilk
// indirmenin YAZARIN yayinladigi sey oldugunu soylemez — ele gecirilmis bir
// kayit defteri ilk kurulumda da sahte paket verebilir. Imza bunu kapatir:
// paket yazarinin ozel anahtariyla imzalanir, kullanici `[trust] keys`te
// yazarin genel anahtarina guvenir.
//
// Bicimler (hepsi metin):
//   genel anahtar   "ed25519:<base64 32 bayt>"
//   ozel anahtar    PEM (PKCS#8), `tulpar pkg keygen` uretir — dosya 0600
//   imza (.sig)     "ed25519:<base64 64 bayt>\n" — paketin HAM baytlari uzerine
// OpenSSL'siz derlemede butun islemler hata dondurur (sessiz "dogrulandi" YOK).
#ifndef TULPAR_PKG_IMZA_HPP
#define TULPAR_PKG_IMZA_HPP

#include <string>

namespace tulpar {

bool imza_mevcut();  // bu derlemede Ed25519 var mi (OpenSSL)
bool imza_keygen(std::string &priv_pem, std::string &pub_text, std::string &err);
bool imza_pub_of(const std::string &priv_pem, std::string &pub_text, std::string &err);
bool imza_sign(const std::string &data, const std::string &priv_pem, std::string &sig_text,
               std::string &err);
// `sig_text` ".sig" icerigi; `pub_text` "ed25519:..." — dogruysa true.
bool imza_verify(const std::string &data, const std::string &sig_text, const std::string &pub_text,
                 std::string &err);

}  // namespace tulpar

#endif
