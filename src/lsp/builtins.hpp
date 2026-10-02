#ifndef TULPAR_LSP_BUILTINS_HPP
#define TULPAR_LSP_BUILTINS_HPP

#include <cstddef>

namespace tulpar {

// One entry of the builtin signature table. `name` is what the user types,
// `signature` is what we render in hover/completion (e.g. "print(value: any): void"),
// `doc` is a one-line description shown beneath the signature.
struct BuiltinEntry {
    const char *name;
    const char *signature;
    const char *doc;
    // Yerel eklenti fonksiyonu ise eklentinin adi (K303), yerlesikse nullptr.
    const char *extension = nullptr;
};

// Returns a pointer to the builtin table. The table is process-static and
// safe to share. `out_count` receives the number of entries.
const BuiltinEntry *builtin_table(size_t *out_count);

// Lookup by exact name. Returns nullptr if not found. Yerlesik tabloda
// yoksa yuklu yerel eklentilerin fonksiyonlarina bakar (K303).
const BuiltinEntry *builtin_lookup(const char *name);

// Yuklu yerel eklentilerin fonksiyonlari, BuiltinEntry olarak (tamamlama).
// Eklenti listesi buyudukce yeniden kurulur; isaretciler bir sonraki
// cagriya kadar gecerli.
const BuiltinEntry *extension_entries(size_t *out_count);

}  // namespace tulpar

#endif  // TULPAR_LSP_BUILTINS_HPP
