// Tulpar Type Inference Module
// Full static type inference for compile-time type checking

#ifndef TULPAR_TYPEINFER_H
#define TULPAR_TYPEINFER_H

#include "../parser/ast_nodes.hpp"
#include <optional>
#include <string>
#include <set>
#include <unordered_map>
#include <vector>

struct InferredTypeInfo {
  DataType type;
  std::optional<std::string> custom_type_name;
  bool is_inferred;
};

// Symbol entry for type tracking
struct TypeSymbol {
  DataType type;
  std::optional<std::string> custom_type_name;
  bool is_mutable; // const değilse true
  bool is_moved;   // move edilmişse true
};

struct FunctionSignature {
  DataType return_type;
  std::optional<std::string> return_custom_type;
  std::vector<DataType> param_types;
};

// User-declared struct (`struct Point { int x; int y; }`). Field
// types are tracked so a future PR can validate `<TypeName> ident;`
// declarations and (Plan 04 PR3+) drive unboxed LLVM struct codegen.
// Order is preserved — codegen emits fields in declaration order.
struct StructTypeInfo {
  std::vector<std::string> field_names;
  std::vector<DataType> field_types;
  std::vector<std::optional<std::string>> field_custom_types;
};

// Type checker context
struct TypeInferContext {
  std::unordered_map<std::string, TypeSymbol> symbols;
  std::unordered_map<std::string, FunctionSignature> functions;
  std::unordered_map<std::string, StructTypeInfo> struct_types;
  // Kullanicinin (bu dosya + ice aktarilan moduller) tanimladigi fonksiyon
  // adlari. `functions` yerlesikleri de tutuyor; `call("ad")` yalniz
  // kullanici fonksiyonunu cagirabildigi icin ayri kume (K009).
  std::set<std::string> user_functions;
  // K068: `async func` cagrisinin sonucu bir FUTURE (calisma zamaninda
  // promise). async fonksiyon adi -> bildirilen donus tipi (await sonrasi T);
  // future tutan semboller -> T.
  std::unordered_map<std::string, DataType> async_fns;
  // K041: `@no_alloc` gecisli denetimi icin fonksiyon tanimlari (ana program
  // + ice aktarilan moduller; modul AST'leri burada yasatiliyor ki
  // isaretciler gecerli kalsin).
  std::unordered_map<std::string, const FunctionDecl *> fn_decls;
  std::vector<std::unique_ptr<ASTNode>> module_asts;
  std::unordered_map<std::string, DataType> future_symbols;
  bool current_is_async = false;
  // K043: takma adsiz ice aktarilan modullerin ust duzey fonksiyonlari
  // hangi modulden geldi (ad -> (modul, satir)). Iki FARKLI modul ayni adi
  // tanimlarsa ilki sessizce kazaniyordu.
  std::unordered_map<std::string, std::pair<std::string, int>> module_fn_origin;
  // Ana dosyanin kendi ust duzey fonksiyonlari (ad -> satir): modulun ayni
  // adli fonksiyonunu programin TAMAMINDA golgeler (K043).
  std::unordered_map<std::string, int> local_fn_line;
  // K027: NOMINAL enum. Enum ayristiricida int'e katlaniyor; adlari burada
  // izleniyor: uyeler (match tamligi), enum tipli semboller, fonksiyonlarin
  // enum donus/parametre tipleri.
  std::unordered_map<std::string, std::vector<std::pair<std::string, long long>>> enum_members;
  std::unordered_map<std::string, std::string> enum_symbols;
  std::unordered_map<std::string, std::string> fn_return_enum;
  std::unordered_map<std::string, std::vector<std::string>> fn_param_enum;
  std::string current_return_enum;   // gezilen fonksiyonun enum donus tipi
  // K061: struct (TYPE_CUSTOM) ADLARI — kullanici fonksiyonunun donus ve
  // parametre struct adlari. Sembollerinki TypeSymbol::custom_type_name'de.
  // Ad bilinmiyorsa kayit yok: karsilastirma yalniz iki taraf da biliniyorsa.
  std::unordered_map<std::string, std::string> fn_return_custom;
  std::unordered_map<std::string, std::vector<std::string>> fn_param_custom;

  // P23 — COZUM ile BASLATMA ayri sorulardir.
  //
  // `global_decl_line` on-gecişte doluyor: ad -> bildirim satiri. Cozum
  // (ad -> tip) artik siradan bagimsiz; ama UST DUZEY calisma sirasi
  // hala gercek. Bildirim satirindan once OKUNAN bir global calisma
  // zamaninda sifirdir, ve bu sessizce oluyordu (`print(sayac); int
  // sayac = 5;` -> "0" basiyor, tek bir tani yok).
  //
  // `initialized_globals` ana gezinti ust duzey bir bildirime VARDIKCA
  // doluyor. Ikisinin farki, "cozuldu ama henuz baslatilmadi" durumunu
  // "bulunamadi"dan AYIRAN tek bilgi — ve o ayrim, #7'nin (bir sentinel
  // iki anlam tasiyamaz) bu dosyadaki karsiligi.
  std::unordered_map<std::string, int> global_decl_line;
  std::set<std::string> initialized_globals;

  // Current function context (for return type checking)
  DataType current_return_type;
  std::string current_function_name;

  // Error tracking
  int error_count;
  std::string last_error;

  // When true, diagnostics are formatted as `[typecheck]` warnings
  // (informational, non-fatal) instead of `Type Error:` lines, and the
  // optional `source_path` is folded in so editors can jump to the file.
  // Used by the build/run pre-pass (`typeinfer_emit_warnings`); the
  // standalone `tulpar typecheck` subcommand keeps the default error mode.
  bool warning_mode;
  std::string source_path;

  // Program `import "x"` içeriyor mu? İçeriyorsa, yerel struct tablosunda
  // bulunmayan bir custom-type "Unknown type" uyarısı VERMEZ — tipi import
  // edilen modülden gelmiş olabilir (typeinfer modül kaynağını parse etmiyor,
  // aynı sebeple import edilen bilinmeyen fonksiyonlara da uyarı vermez).
  bool has_imports = false;
  // K053: `var` global'lerin tipini on-geciste cikarirken infer_expr'in
  // tanilari BASILMASIN / SAYILMASIN (ana gezinti ayni ifadeyi yeniden
  // denetler; ikinci kez raporlamak cift tani olurdu).
  bool silent = false;
};

// ============================================================================
// API Functions
// ============================================================================

// Create/destroy context
TypeInferContext *typeinfer_create(void);
void typeinfer_destroy(TypeInferContext *ctx);

// Main inference functions
DataType typeinfer_expression(TypeInferContext *ctx, const ASTNode *expr);
void typeinfer_statement(TypeInferContext *ctx, const ASTNode *stmt);
void typeinfer_program(TypeInferContext *ctx, const ASTNode *program);

// Symbol table management
void typeinfer_add_symbol(TypeInferContext *ctx, const char *name, DataType type);
DataType typeinfer_lookup_symbol(TypeInferContext *ctx, const char *name);
void typeinfer_mark_moved(TypeInferContext *ctx, const char *name);
int typeinfer_is_moved(TypeInferContext *ctx, const char *name);

// Function registration
void typeinfer_register_function(TypeInferContext *ctx, const char *name, 
                                  DataType return_type, DataType *param_types, int param_count);
DataType typeinfer_get_function_return_type(TypeInferContext *ctx, const char *name);

// Ad, typeinfer'in yerlesik katalogunda mi (K303: bir yerel eklenti
// fonksiyonu yerlesik adini tasiyamaz — yukleme aninda reddedilir).
bool typeinfer_is_builtin_name(const char *name);

// `tulpar analyze` (K157): bu dosyanin UST DUZEY her fonksiyonu icin
// `@no_alloc` kuraliyla (ayni gecisli, beyaz listeli denetim) ayirma raporu.
// `typeinfer_program`dan SONRA cagrilir. Kaynak sirasiyla; neden bos ise
// fonksiyon kurala gore ayirmasiz.
struct TypeinferAllocRow {
  std::string name;
  int line;
  bool no_alloc_marked;
  std::string reason;   // "" = ayirmasiz
};
std::vector<TypeinferAllocRow> typeinfer_alloc_report(TypeInferContext *ctx,
                                                      const ASTNode *program);

// Error handling
int typeinfer_has_errors(TypeInferContext *ctx);
const char *typeinfer_get_last_error(TypeInferContext *ctx);

// Utility: Convert DataType to string for error messages
const char *datatype_to_string(DataType type);

// Type compatibility checking
int types_compatible(DataType a, DataType b);
DataType promote_types(DataType a, DataType b);  // For binary ops

#endif // TULPAR_TYPEINFER_H
