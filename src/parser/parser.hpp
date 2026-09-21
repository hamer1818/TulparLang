#ifndef TULPAR_PARSER_HPP
#define TULPAR_PARSER_HPP

#include "../lexer/lexer.hpp"
#include "ast_nodes.hpp"
#include <vector>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

// ============================================================================
// Modern C++ Parser Class
// ============================================================================

class Parser {
private:
    std::vector<Token> tokens_;
    size_t position_;
    
    // Helper methods
    const Token& current() const;
    const Token& peek(int offset = 1) const;
    void advance();
    bool match(TulparTokenType type);
    bool check(TulparTokenType type) const;
    Token expect(TulparTokenType type, const std::string& error_msg);
    bool is_at_end() const;
    
    // Precedence-based parsing
    int get_precedence(TulparTokenType op) const;
    
    // Expression parsing (recursive descent)
    std::unique_ptr<ASTNode> parse_expression(int precedence = 0);
    std::unique_ptr<ASTNode> parse_primary();
    // Desugar a t-string template ("a {x} b") into a `"" + "a " + x + " b"`
    // concatenation chain. `tmpl` is the raw inner text from the lexer.
    std::unique_ptr<ASTNode> build_tstring(const std::string& tmpl, SourceLocation loc);
    std::unique_ptr<ASTNode> parse_binary_op(std::unique_ptr<ASTNode> left, int precedence);
    std::unique_ptr<ASTNode> parse_unary();
    std::unique_ptr<ASTNode> parse_postfix(std::unique_ptr<ASTNode> expr);
    std::unique_ptr<ASTNode> parse_call(std::unique_ptr<ASTNode> callee);
    std::unique_ptr<ASTNode> parse_array_access(std::unique_ptr<ASTNode> object);
    std::unique_ptr<ASTNode> parse_array_literal();
    std::unique_ptr<ASTNode> parse_object_literal();
    // Restricted-context object literal accepting both string and bare
    // identifier keys, used only for typed-struct VAR_DECL initializers
    // (`Point p = { x: 1, y: 2 };`). Generic object literals continue to
    // require string keys via parse_object_literal so the relaxed key
    // form does not introduce parser ambiguity at expression position.
    std::unique_ptr<ASTNode> parse_struct_literal_init();
    
    // Statement parsing
    std::unique_ptr<ASTNode> parse_statement();
    std::unique_ptr<ASTNode> parse_variable_decl();
    std::unique_ptr<ASTNode> parse_function_decl();
    std::unique_ptr<ASTNode> parse_type_decl();
    std::unique_ptr<ASTNode> parse_enum_decl();
    std::unique_ptr<ASTNode> parse_if_statement();
    std::unique_ptr<ASTNode> parse_while_loop();
    std::unique_ptr<ASTNode> parse_for_loop();
    std::unique_ptr<ASTNode> parse_for_in_loop();
    std::unique_ptr<ASTNode> parse_return_statement();
    std::unique_ptr<ASTNode> parse_break_statement();
    std::unique_ptr<ASTNode> parse_continue_statement();
    std::unique_ptr<ASTNode> parse_try_catch();
    std::unique_ptr<ASTNode> parse_throw_statement();
    std::unique_ptr<ASTNode> parse_import_statement();
    std::unique_ptr<ASTNode> parse_block();
    std::unique_ptr<ASTNode> parse_expression_statement();
    
    // Type parsing
    DataType parse_type();
    // `T[N]` ayristirildiginda N burada kalir (yoksa 0). parse_type'in donus
    // tipini degistirmemek icin uye: cagri yerlerinin cogu N'i umursamiyor.
    // Yalniz bildirim yolu okuyor ve HEMEN tuketiyor.
    int last_fixed_array_n_ = 0;
    std::string parse_custom_type_name();
    
    // Error handling
    void error(const std::string& message);

    // ---- `const` denetimi (G5) -------------------------------------------
    // Ayristirici normalde sembol tablosu TUTMAZ; const icin minimal bir
    // kapsam yigini tutuyor cunku "yeniden atama hatadir" kurali tip
    // bilgisi ISTEMIYOR, yalniz baglamayi bilmeyi istiyor — ve
    // ayristiricida yapilinca `--strict` bayragina bagli OLMAYAN sert bir
    // hata oluyor (typeinfer'deki her sey varsayilan olarak UYARI).
    //
    // Her kapsam ad -> const_mu esleme listesi. Golgeleme calisir: icteki
    // const OLMAYAN bildirim distaki const'u kapatir. Bildirimin
    // KAYDEDILMEDIGI egzotik bir baglama bicimi (match yapisokum deseni)
    // varsa sonuc YANLIS POZITIF olabilirdi; bu bugun imkansiz cunku const
    // yeni ve depoda hic kullanilmiyor — yani mevcut kodun tamami icin bu
    // yigin bostur.
    std::vector<std::vector<std::pair<std::string, bool>>> decl_scopes_;
    void scope_push();
    void scope_pop();
    void scope_declare(const std::string& name, bool is_const);
    bool name_is_const(const std::string& name) const;
    // Hedef const ise ayristirma hatasi firlatir.
    void reject_const_write(const Token& name_tok);

    // ---- `enum` (P0.2, 2026-09-21) --------------------------------------
    // Enum TAMAMEN ayristirici seviyesinde bir sekerdir: `Ekran.MENU`
    // IntLiteral'e katlanir, `Ekran` tip adi TYPE_INT'e cozulur. Bunun icin
    // ayristirici enum tablosunu bildirim SIRASINDAN BAGIMSIZ bilmeli —
    // `func f(): Ekran` enum bildiriminden once gelebilir. Cozum: parse()
    // basinda token dizisi uzerinde tek gecisli ON TARAMA (prescan_enums):
    // yalniz `enum AD { UYE [= SAYI], ... }` kalibini toplar, hata VERMEZ;
    // sert denetim (yinelenen uye/ad, gecersiz deger, ust duzey disi)
    // parse_enum_decl'de, kaynak sirasiyla, tr_en mesajiyla yapilir.
    // `decl_token`, ayni adin ikinci bildirimini ("yeniden tanimlandi")
    // on taramanin gordugu ilk bildirimden AYIRT ETMEK icin tutulur.
    struct EnumInfo {
        std::vector<std::pair<std::string, long long>> members;
        size_t decl_token = 0;
    };
    std::unordered_map<std::string, EnumInfo> enums_;
    void prescan_enums();
    bool is_enum_name(const std::string& name) const;
    // Uye yoksa nullptr.
    const long long* enum_member_value(const std::string& enum_name,
                                       const std::string& member) const;

    // ---- coklu donus / tuple (P0.1, 2026-09-21) --------------------------
    // `func f(): (float, float) { return a, b; }`, `float x, y = f();`,
    // `x, y = f();` — TAMAMEN ayristirici sekeri. Tip listesi icin
    // `struct __tup_float_float { float _0; float _1; }` sentezlenir (ad
    // yalniz tiplerden; programin basina BIR KEZ eklenir), fonksiyon o
    // struct'i doner (P0.3 sayesinde native res-ptr: sifir tahsis),
    // bildirim/atama alanlari acar. `return a, b;` bir Block'a acilir
    // (`{ __T __r; __r._0 = a; __r._1 = b; return __r; }`); bildirim ise
    // acilamaz — Block kapsam acar, dx/dz disarida gorunmez olurdu — bu
    // yuzden `pending_after_`: parse_statement'in ARDINA, AYNI seviyeye
    // eklenecek deyimler; parse_block / parse() her deyimden sonra bosaltir,
    // suslu parantezsiz govdeler (if/while/for) reddeder.
    // Callee'nin tuple tipi `x, y = f()` ve `var a, b = f()` icin gerekli:
    // prescan_tuple_sigs `func AD ( ... ) : ( T, T )` kalibini token
    // dizisinden toplar — v1'de callee AYNI DOSYADA adlandirilmis bir
    // fonksiyon olmali (closure/degisken cagrisi, import edilen modul: hata).
    struct TupleElem {
        DataType type = TYPE_UNKNOWN;
        std::optional<std::string> custom;
    };
    std::unordered_map<std::string, std::vector<TupleElem>> tuple_sigs_;
    std::unordered_map<std::string, std::vector<TupleElem>> synth_tuple_structs_;
    std::vector<TupleElem> current_tuple_types_;   // bos = tuple donmuyor
    std::vector<std::unique_ptr<ASTNode>> pending_after_;
    int tuple_tmp_counter_ = 0;
    void prescan_tuple_sigs();
    std::vector<TupleElem> parse_tuple_type_list();
    std::string tuple_struct_name(const std::vector<TupleElem>& elems);
    const std::vector<TupleElem>* tuple_sig_of_call(const ASTNode* call) const;
    std::unique_ptr<ASTNode> parse_tuple_var_decl(SourceLocation loc,
                                                  DataType first_type,
                                                  std::optional<std::string> first_custom,
                                                  const std::string& first_name,
                                                  bool is_const);
    std::unique_ptr<ASTNode> parse_tuple_assignment();
    std::unique_ptr<ASTNode> make_tuple_temp_decl(const std::string& tmp,
                                                  const std::string& struct_name,
                                                  std::unique_ptr<ASTNode> init,
                                                  SourceLocation loc);
    std::unique_ptr<ASTNode> make_tuple_field(const std::string& var, size_t idx,
                                              SourceLocation loc);
    void drain_pending(std::vector<std::unique_ptr<ASTNode>>& out);
    void reject_pending();

public:
    // Constructor
    explicit Parser(std::vector<Token> tokens);
    
    // Main parsing method - returns Program node
    std::unique_ptr<ASTNode> parse();
};

// ============================================================================
// Legacy C API (for backward compatibility during transition)
// ============================================================================

// Old C-style AST node (kept for compatibility)
typedef enum {
  AST_INT_LITERAL,
  AST_FLOAT_LITERAL,
  AST_STRING_LITERAL,
  AST_BOOL_LITERAL,
  AST_NULL_LITERAL,
  AST_ARRAY_LITERAL,
  AST_OBJECT_LITERAL,
  AST_IDENTIFIER,
  AST_BINARY_OP,
  AST_UNARY_OP,
  AST_FUNCTION_CALL,
  AST_ARRAY_ACCESS,
  AST_TYPE_DECL,
  AST_VARIABLE_DECL,
  AST_ASSIGNMENT,
  AST_COMPOUND_ASSIGN,
  AST_INCREMENT,
  AST_DECREMENT,
  AST_FUNCTION_DECL,
  AST_RETURN,
  AST_IF,
  AST_WHILE,
  AST_FOR,
  AST_FOR_IN,
  AST_BREAK,
  AST_CONTINUE,
  AST_TRY_CATCH,
  AST_THROW,
  AST_IMPORT,
  AST_BLOCK,
  AST_PROGRAM,
  AST_LAMBDA,
  AST_MATCH,
  AST_AWAIT,
  AST_TERNARY,
  // `enum` bildirimi (P0.2). Codegen icin no-op; `name` = enum adi,
  // `field_names[i]` = uye adi, `field_defaults[i]` = AST_INT_LITERAL deger.
  // Sona eklendi (numaralama kaymasin).
  AST_ENUM_DECL
} ASTNodeType;

// Old C-style AST node structure (kept for legacy code compatibility)
typedef struct ASTNode_C {
  ASTNodeType type;
  int line;
  int column;

  union {
    long long int_value;
    // DOUBLE olmali: calisma zamani (VMValue payload, backend->float_type =
    // LLVMDoubleType) 64-bit. Burasi `float` iken her kaynak literali
    // SESSIZCE float32'ye kirpiliyordu — `float pi = 3.141592653589793;`
    // 3.1415927410125732 olarak derleniyor, ~9 hane gidiyor, uyari cikmiyor.
    // print() %g ile 6 hane bastigi icin gozle de gorunmuyordu.
    // Olculdu 2026-09-05; regresyon: tests/float_precision.test.tpr
    double float_value;
    char *string_value;
    int bool_value;
  } value;

  struct ASTNode_C *left;
  struct ASTNode_C *right;
  TulparTokenType op;

  char *name;
  DataType data_type;
  struct ASTNode_C **field_types_nodes;
  char **field_names;
  DataType *field_types;
  char **field_custom_types;
  int field_count;
  struct ASTNode_C **field_defaults;

  struct ASTNode_C **parameters;
  int param_count;
  struct ASTNode_C *body;
  DataType return_type;
  char *return_custom_type;
  char *receiver_type_name;
  struct ASTNode_C *receiver;
  struct ASTNode_C *callee;
  
  uint8_t is_moved;
  uint8_t is_async; // AST_FUNCTION_DECL: declared with `async`

  struct ASTNode_C *condition;
  struct ASTNode_C *then_branch;
  struct ASTNode_C *else_branch;

  struct ASTNode_C *init;
  struct ASTNode_C *increment;
  struct ASTNode_C *iterable;

  struct ASTNode_C **statements;
  int statement_count;

  struct ASTNode_C *return_value;

  struct ASTNode_C **arguments;
  int argument_count;
  char **argument_names;

  struct ASTNode_C **elements;
  int element_count;
  struct ASTNode_C *index;

  char **object_keys;
  struct ASTNode_C **object_values;
  int object_count;

  struct ASTNode_C *try_block;
  struct ASTNode_C *catch_block;
  struct ASTNode_C *finally_block;
  char *catch_var;
  struct ASTNode_C *throw_expr;

} ASTNode_C;

// Old C-style Parser structure
typedef struct {
  Token **tokens;
  int position;
  Token *current_token;
  int token_count;
} Parser_C;

extern "C" {
    Parser_C *parser_create(Token **tokens, int token_count);
    void parser_free(Parser_C *parser);
    ASTNode_C *parser_parse(Parser_C *parser);
    ASTNode_C *ast_node_create(ASTNodeType type);
    void ast_node_free(ASTNode_C *node);
    void ast_print(ASTNode_C *node, int indent);

    // Provide source text + filename so parse-time diagnostics can render a
    // Rust-style line excerpt + caret rather than a one-liner. Both pointers
    // are borrowed (caller owns) and live until the next parse_parse call.
    // Pass null/null to disable enrichment.
    void parser_set_diagnostic_context(const char *source_text,
                                       const char *source_filename);

    // Suppress the pretty-render side of Parser::error. The parse still
    // throws, the catch upstream still recovers — but no diagnostic
    // reaches stderr. Callers that re-parse later (typeinfer pre-pass)
    // use this to avoid double-reporting the same syntax error.
    void parser_set_quiet(int quiet);

    // How many parse errors fired during the last `Parser::parse()`. The
    // parser recovers and returns a partial AST regardless; pre-pass
    // consumers (typeinfer) read this to decide whether the AST is
    // trustworthy enough to walk.
    int parser_get_error_count(void);
}

#endif // TULPAR_PARSER_HPP
