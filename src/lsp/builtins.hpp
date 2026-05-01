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
};

// Returns a pointer to the builtin table. The table is process-static and
// safe to share. `out_count` receives the number of entries.
const BuiltinEntry *builtin_table(size_t *out_count);

// Lookup by exact name. Returns nullptr if not found.
const BuiltinEntry *builtin_lookup(const char *name);

}  // namespace tulpar

#endif  // TULPAR_LSP_BUILTINS_HPP
