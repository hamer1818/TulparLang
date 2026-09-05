#!/usr/bin/env python3
"""ASTNode_C'nin TUM cocuk alanlari llvm_array_shape.cpp'deki gezicide var mi?

Neden gerekli: gezici bir alt agaci atlarsa, dongu-sekli kanitindaki
"govdede cagri yok" iddiasi DELINIR — atlanan dalda bir `push` durabilir ve
onbellek bayat kalir (bellek bozulmasi). Alan eklemek serbest, atlamak degil.
Bu denetim ./build.sh suites icinde kosuyor.
"""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
struct = re.search(r'typedef struct ASTNode_C \{(.*?)\n\} ASTNode_C;',
                   (root/'src/parser/parser.hpp').read_text(), re.S).group(1)
declared = set(re.findall(r'struct ASTNode_C \*+\s*([a-z_]+)', struct))
walker = (root/'src/aot/llvm_array_shape.cpp').read_text()
body = walker[walker.index('static bool walk_all'):walker.index('static bool visit_kind_ok')]
covered = set(re.findall(r'n->([a-z_]+)', body))
missing = sorted(declared - covered)
if missing:
    print("HATA: gezici su cocuk alanlarini atliyor: " + ", ".join(missing))
    print("  -> src/aot/llvm_array_shape.cpp icindeki walk_all()'a ekle.")
    sys.exit(1)
print(f"ast_child_fields_audit: {len(declared)} cocuk alaninin hepsi geziliyor")
