#ifndef TULPAR_LOCALIZATION_HPP
#define TULPAR_LOCALIZATION_HPP

namespace tulpar {
namespace i18n {

bool is_turkish_locale();
const char *tr_en(const char *tr_text, const char *en_text);
const char *tr_for_en(const char *en_text);

} // namespace i18n
} // namespace tulpar

#endif // TULPAR_LOCALIZATION_HPP

