#include "lexer.hpp"
#include "../common/localization.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// <iostream> BILEREK YOK: hic kullanilmiyordu ve `std::ios_base::Init`
// statik kurucusunu `libtulpar_runtime.a`ya sokuyordu (lexer.cpp her AOT
// ikilisine giriyor). Cikarilinca arsivden kalkti; ikili boyutu
// DEGISMEDI, cunku akislari baska bir birim de cekiyor — yine de
// kullanilmayan bir bagimliligi tasimanin anlami yok.
#include <unordered_map>

// ============================================================================
// Keyword Mapping Table (Turkish-first with English/ASCII aliases)
// ============================================================================

static const std::unordered_map<std::string, TulparTokenType>& get_keyword_map() {
    static const std::unordered_map<std::string, TulparTokenType> KEYWORD_MAP = {
        // Data Types - Turkish (primary)
        {"tamsayı", TOKEN_INT_TYPE},
        {"ondalık", TOKEN_FLOAT_TYPE},
        {"metin", TOKEN_STR_TYPE},
        {"mantıksal", TOKEN_BOOL_TYPE},
        {"dizi", TOKEN_ARRAY_TYPE},
        {"diziTamsayı", TOKEN_ARRAY_INT},
        {"diziOndalık", TOKEN_ARRAY_FLOAT},
        {"diziMetin", TOKEN_ARRAY_STR},
        {"diziMantıksal", TOKEN_ARRAY_BOOL},
        {"diziJson", TOKEN_ARRAY_JSON},
        {"json", TOKEN_JSON_TYPE},

        // Data Types - English (aliases)
        {"int", TOKEN_INT_TYPE},
        {"float", TOKEN_FLOAT_TYPE},
        {"str", TOKEN_STR_TYPE},
        {"bool", TOKEN_BOOL_TYPE},
        {"array", TOKEN_ARRAY_TYPE},
        {"arrayInt", TOKEN_ARRAY_INT},
        {"arrayFloat", TOKEN_ARRAY_FLOAT},
        {"arrayStr", TOKEN_ARRAY_STR},
        {"arrayBool", TOKEN_ARRAY_BOOL},
        {"arrayJson", TOKEN_ARRAY_JSON},
        
        // Data Types - ASCII Turkish (aliases)
        {"tamsayi", TOKEN_INT_TYPE},
        {"ondalik", TOKEN_FLOAT_TYPE},
        {"mantiksal", TOKEN_BOOL_TYPE},
        {"diziTamsayi", TOKEN_ARRAY_INT},
        {"diziOndalik", TOKEN_ARRAY_FLOAT},
        {"diziMantiksal", TOKEN_ARRAY_BOOL},
        
        // Control Flow - Turkish (primary)
        {"eğer", TOKEN_IF},
        {"yoksa", TOKEN_ELSE},
        {"iken", TOKEN_WHILE},
        {"için", TOKEN_FOR},
        {"içinde", TOKEN_IN},
        {"dur", TOKEN_BREAK},
        {"devam_et", TOKEN_CONTINUE},
        {"geri_döndür", TOKEN_RETURN},
        {"tekrar", TOKEN_WHILE},
        
        // Control Flow - English (aliases)
        {"if", TOKEN_IF},
        {"else", TOKEN_ELSE},
        {"while", TOKEN_WHILE},
        {"for", TOKEN_FOR},
        {"match", TOKEN_MATCH},
        {"eşle", TOKEN_MATCH},
        {"esle", TOKEN_MATCH},
        {"async", TOKEN_ASYNC},
        {"await", TOKEN_AWAIT},
        {"bekle", TOKEN_AWAIT},
        {"in", TOKEN_IN},
        {"break", TOKEN_BREAK},
        {"continue", TOKEN_CONTINUE},
        {"return", TOKEN_RETURN},
        
        // Control Flow - ASCII Turkish (aliases)
        {"eger", TOKEN_IF},
        {"icin", TOKEN_FOR},
        {"icinde", TOKEN_IN},
        {"devam", TOKEN_CONTINUE},
        {"geri_dondur", TOKEN_RETURN},
        {"don", TOKEN_RETURN},
        
        // Functions - Turkish (primary)
        {"fonksiyon", TOKEN_FUNC},
        {"tip", TOKEN_TYPE_KW},
        {"değişken", TOKEN_VAR},
        {"taşı", TOKEN_MOVE},
        
        // Functions - English (aliases)
        {"func", TOKEN_FUNC},
        {"type", TOKEN_TYPE_KW},
        // `struct` is a familiar spelling for users coming from C/Rust/Go;
        // it parses identically to `type Name { ... }` (same AST, same
        // codegen). This keeps the surface ergonomic without forking the
        // declaration semantics.
        {"struct", TOKEN_TYPE_KW},
        {"var", TOKEN_VAR},
        // `const x` — DEGERI degil BAGLAMAYI sabitler: `const int n = 5;`
        // sonrasinda `n = 6;` bir AYRISTIRMA HATASIDIR. Derin degismezlik
        // YOK: `const` bir dizinin icerigini dondurmaz.
        //
        // TURKCE TAKMA AD YOK ve bu bir ihmal DEGIL, olcum: `sabit`
        // denendi ve o gunun bir paketi (motor koprusu testi, simdi
        // tulpar-engine deposunda) ANINDA dustu — orada
        // `int sabit = -1;` diye bir DEGISKEN vardi. Yani `sabit`i anahtar
        // kelime yapmak calisan bir testi kiriyor. Bu, deponun bilinen
        // tuzagi (`move`/`don` ayni sekilde adlari calmisti); yeni bir
        // anahtar kelime eklerken once `grep -rn --include='*.tpr'`
        // yapilmali. `degismez` de ayni riski tasidigi icin secilmedi.
        {"const", TOKEN_CONST},
        // `let` is an alias for `var` (type-inferred local) — familiar to
        // JS/Rust/Swift users. Same token, same codegen as `var`.
        {"let", TOKEN_VAR},
        {"move", TOKEN_MOVE},
        
        // Functions - ASCII Turkish (aliases)
        {"degisken", TOKEN_VAR},
        {"tasi", TOKEN_MOVE},
        
        // Legacy Turkish aliases (backward compat)
        {"işlev", TOKEN_FUNC},
        {"fonk", TOKEN_FUNC},
        {"döndür", TOKEN_RETURN},
        {"değilse", TOKEN_ELSE},
        {"dondur", TOKEN_RETURN},
        {"degilse", TOKEN_ELSE},
        {"islev", TOKEN_FUNC},
        
        // Exception Handling - Turkish (primary)
        {"dene", TOKEN_TRY},
        {"yakala", TOKEN_CATCH},
        {"sonunda", TOKEN_FINALLY},
        {"fırlat", TOKEN_THROW},
        
        // Exception Handling - English (aliases)
        {"try", TOKEN_TRY},
        {"catch", TOKEN_CATCH},
        {"finally", TOKEN_FINALLY},
        {"throw", TOKEN_THROW},
        
        // Exception Handling - ASCII Turkish (aliases)
        {"firlat", TOKEN_THROW},
        
        // Import - Turkish (primary)
        {"içe_aktar", TOKEN_IMPORT},
        
        // Import - English (aliases)
        {"import", TOKEN_IMPORT},
        
        // Import - ASCII Turkish (aliases)
        {"ice_aktar", TOKEN_IMPORT},
        
        // Boolean Literals - Turkish (primary)
        {"doğru", TOKEN_TRUE},
        {"yanlış", TOKEN_FALSE},
        
        // Boolean Literals - English (aliases)
        {"true", TOKEN_TRUE},
        {"false", TOKEN_FALSE},
        {"null", TOKEN_NULL},

        // Boolean Literals - ASCII Turkish (aliases)
        {"dogru", TOKEN_TRUE},
        {"yanlis", TOKEN_FALSE}
    };
    return KEYWORD_MAP;
}

// ============================================================================
// Token Class Implementation
// ============================================================================

void Token::print() const {
    const char* type_names[] = {
        "INT_TYPE", "FLOAT_TYPE", "STR_TYPE", "BOOL_TYPE",
        "ARRAY_TYPE", "ARRAY_INT", "ARRAY_FLOAT", "ARRAY_STR", "ARRAY_BOOL", "ARRAY_JSON",
        "FUNC", "TYPE_KW", "RETURN", "IF", "ELSE", "WHILE", "FOR", "IN",
        "BREAK", "CONTINUE", "TRY", "CATCH", "FINALLY", "THROW", "IMPORT",
        "TRUE", "FALSE", "MOVE", "VAR",
        "IDENTIFIER", "INT_LITERAL", "FLOAT_LITERAL", "STRING_LITERAL",
        "PLUS", "MINUS", "MULTIPLY", "DIVIDE", "ASSIGN",
        "EQUAL", "NOT_EQUAL", "LESS", "GREATER", "LESS_EQUAL", "GREATER_EQUAL",
        "AND", "OR", "BANG", "PLUS_PLUS", "MINUS_MINUS",
        "PLUS_EQUAL", "MINUS_EQUAL", "MULTIPLY_EQUAL", "DIVIDE_EQUAL",
        "MODULO_EQUAL",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACKET", "RBRACKET",
        "SEMICOLON", "COMMA", "COLON", "DOT",
        "EOF", "ERROR"
    };
    
    // SINIR DENETIMI: bu tablo enum ile zaten SENKRON DEGIL (MODULO,
    // FAT_ARROW, PIPE, DOTDOT ve yeni bit tokenlari yok). Tablo disina
    // dusen bir token'i yazdirmak tanimsiz okuma demekti.
    const size_t kNames = sizeof(type_names) / sizeof(type_names[0]);
    if (static_cast<size_t>(type_) >= kNames) {
        printf("Token(#%d, \"%s\", line: %d, col: %d)\n",
               static_cast<int>(type_), value_.c_str(), line_, column_);
        return;
    }
    printf("Token(%s, \"%s\", line: %d, col: %d)\n",
           type_names[type_],
           value_.c_str(),
           line_,
           column_);
}

// ============================================================================
// Lexer Class Implementation
// ============================================================================

Lexer::Lexer(const std::string& source)
    : source_(source),
      position_(0),
      line_(1),
      column_(0),
      current_char_(source.empty() ? '\0' : source[0]) {
}

void Lexer::advance() {
    position_++;
    column_++;
    
    if (position_ < source_.length()) {
        current_char_ = source_[position_];
    } else {
        current_char_ = '\0';
    }
}

char Lexer::peek(size_t offset) const {
    size_t peek_pos = position_ + offset;
    if (peek_pos < source_.length()) {
        return source_[peek_pos];
    }
    return '\0';
}

void Lexer::skip_whitespace() {
    while (current_char_ != '\0' && std::isspace(current_char_)) {
        if (current_char_ == '\n') {
            line_++;
            column_ = 0;
        }
        advance();
    }
}

void Lexer::skip_comment() {
    if (current_char_ == '/' && peek() == '/') {
        // Single line comment
        while (current_char_ != '\0' && current_char_ != '\n') {
            advance();
        }
    }
}

void Lexer::skip_block_comment() {
    if (current_char_ == '/' && peek() == '*') {
        // Block comment: /* ... */
        int start_line = line_;
        int start_col = column_;
        advance(); // '/'
        advance(); // '*'
        
        while (current_char_ != '\0') {
            if (current_char_ == '*' && peek() == '/') {
                advance(); // '*'
                advance(); // '/'
                return;
            }
            if (current_char_ == '\n') {
                line_++;
                column_ = 0;
            }
            advance();
        }
        
        // Not terminated
        fprintf(stderr,
                tulpar::i18n::tr_for_en("Lexer Error: Block comment not terminated (started at line %d, col %d)\n"),
                start_line, start_col);
    }
}

// Sayinin hemen ardindan gelemeyecek karakterler (tanimlayici baslangici).
static bool ident_start_char(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
           static_cast<unsigned char>(c) > 127;
}

// Bir karakterin verilen tabandaki basamak degeri; degilse -1.
static int digit_value_in_base(char c, int base) {
    int v;
    if (c >= '0' && c <= '9')      v = c - '0';
    else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
    else return -1;
    return (v < base) ? v : -1;
}

// SAYI-HARF BITISIKLIGI TEK BIR ACIK TANIYA CEVRILIYOR.
//
// `1u`, `2f`, `1e` (eksik us), `1_` (asili ayirici), `0xFFg` — hepsi bugun
// sayiyi bitirip ardindan bir TANIMLAYICI okuyordu ve kullanici
// "Expected ')' after condition" gibi ilgisiz bir mesaj goruyordu.
// Ozellikle `1u` onemli: GLSL/C'deki isaretsiz sonek ve Tulpar'da isaretsiz
// tip YOK (tulpar-engine: docs/FAZ8.md T3) — sessizce `1` saymak yanlis bir zihin
// modeli kurardi, bu yuzden ACIK hata.
//
// Yanlis pozitif riski yok: Tulpar'da "sayi hemen ardindan tanimlayici"
// dizilimi HICBIR gecerli programda yok (olculdu: examples/, lib/, tests/,
// packages/ icinde tek ornek cikmadi; `camera3d`/`sha1` gibi adlar HARFLE
// basladigi icin bu yola hic girmiyor).
static Token number_suffix_error(int line, int column) {
    fprintf(stderr,
            tulpar::i18n::tr_en(
                "Sozcukleyici Hatasi: sayidan hemen sonra harf gelemez — "
                "sayi soneki (`1u`, `2f`) desteklenmiyor (satir %d, sutun %d)\n",
                "Lexer Error: a letter cannot follow a number directly — "
                "numeric suffixes (`1u`, `2f`) are not supported "
                "(line %d, col %d)\n"),
            line, column);
    return Token(TOKEN_ERROR, "", line, column);
}

Token Lexer::read_number() {
    int start_line = line_;
    int start_column = column_;
    std::string buffer;
    bool is_float = false;

    // ------------------------------------------------------------------
    // TABAN ONEKLERI: `0xFF` (onaltilik) ve `0b1010` (ikilik).
    //
    // Neden ikilik de var: bit islemleri gelince maske yazmanin dogal
    // bicimi bu (`0b1011`), ve ayni tarama dongusunu paylastigi icin
    // maliyeti tek bir `base` degiskeni. Sekizlik (`0o`) EKLENMEDI —
    // bugunku kodda hicbir kullanimi yok ve C'nin "bassiz sifir sekizlik"
    // kurali (`010` == 8) sessiz bir tuzak; onu hic acmamak daha iyi.
    //
    // Degeri BURADA cozup onluk metne ceviriyoruz. Ayristirici sayi
    // metnini `std::stoll(deger)` ile okuyor (taban 10); `0x`li metni ona
    // vermek ya sessizce 0 verirdi ya da ikinci bir tabanli okuma yolu
    // acardi. Onluk sayilar ESKI YOLDA kaliyor — us/ondalik/tasma
    // davranisi hic degismesin diye.
    if (current_char_ == '0' && (peek() == 'x' || peek() == 'X' ||
                                 peek() == 'b' || peek() == 'B')) {
        char kind = peek();
        int base = (kind == 'x' || kind == 'X') ? 16 : 2;
        advance(); // '0'
        advance(); // 'x' / 'b'
        unsigned long long acc = 0;
        int digits = 0;
        bool overflow = false;
        while (current_char_ != '\0') {
            if (current_char_ == '_') {
                // Ayirici YALNIZ iki basamak ARASINDA. `0x_F` ve `0xF_`
                // basamak sayilmaz; asagidaki "basamak yok" / kalan `_`
                // tanimlayici yolu bunlari GORUNUR hataya cevirir.
                if (digits == 0 || digit_value_in_base(peek(), base) < 0) break;
                advance();
                continue;
            }
            int d = digit_value_in_base(current_char_, base);
            if (d < 0) break;
            if (acc > (~0ULL - (unsigned long long)d) / (unsigned long long)base)
                overflow = true;
            acc = acc * (unsigned long long)base + (unsigned long long)d;
            digits++;
            advance();
        }
        if (digits == 0) {
            fprintf(stderr,
                    tulpar::i18n::tr_en(
                        "Sozcukleyici Hatasi: '0%c' onekinden sonra basamak "
                        "yok (satir %d, sutun %d)\n",
                        "Lexer Error: no digits after '0%c' prefix "
                        "(line %d, col %d)\n"),
                    kind, start_line, start_column);
            return Token(TOKEN_ERROR, std::string("0") + kind,
                         start_line, start_column);
        }
        if (overflow) {
            fprintf(stderr,
                    tulpar::i18n::tr_en(
                        "Sozcukleyici Hatasi: tabanli sayi 64 bite sigmiyor "
                        "(satir %d, sutun %d)\n",
                        "Lexer Error: base-prefixed number does not fit in "
                        "64 bits (line %d, col %d)\n"),
                    start_line, start_column);
            return Token(TOKEN_ERROR, "0", start_line, start_column);
        }
        if (ident_start_char(current_char_))
            return number_suffix_error(start_line, start_column);
        // 64 bitin TAMAMI yazilabilir: `0xFFFFFFFFFFFFFFFF` -> -1.
        // Tulpar'in `int`i ISARETLI 64 bit ve isaretsiz tip YOK; maske
        // yazan kullanicinin istedigi bit deseni tam olarak budur.
        return Token(TOKEN_INT_LITERAL,
                     std::to_string(static_cast<long long>(acc)),
                     start_line, start_column);
    }

    // Onluk yol (degismedi) + BASAMAK AYIRICI `_`.
    //
    // `1_000_000`. Ayirici yalniz iki BASAMAK arasinda gecerli; `1_`,
    // `1._0`, `_1` degil. Bu kosul yuk tasiyor: gevsek birakilsaydi `x = 1_;`
    // sessizce 1 olurdu. Su anki halde `_` sayinin disinda kalir, ardindan
    // tanimlayici olarak okunur ve ayristirici GORUNUR hata verir.
    while (current_char_ != '\0' &&
           (std::isdigit(current_char_) || current_char_ == '.' ||
            current_char_ == '_')) {
        if (current_char_ == '_') {
            if (buffer.empty() ||
                !std::isdigit(static_cast<unsigned char>(buffer.back())) ||
                !std::isdigit(static_cast<unsigned char>(peek())))
                break;
            advance();
            continue;
        }
        if (current_char_ == '.') {
            if (is_float) break; // Second dot - error
            if (peek() == '.') break; // `..` range op — leave for the lexer
            is_float = true;
        }
        buffer += current_char_;
        advance();
    }

    // Bilimsel gosterim: `1e20`, `1.5e-8`, `2E+3`.
    //
    // Yoktu ve dil KENDI CIKTISINI okuyamiyordu: `print(big)` `1e+20`
    // basiyor ama `1e+20` kaynakta ayristirma hatasi veriyordu. Yani bir
    // programin ciktisini alip kaynaga geri koymak imkansizdi; kucuk/buyuk
    // sabitler de elle sifir sayarak yazilmak zorundaydi.
    //
    // `e` YALNIZCA arkasindan basamak (ya da isaret + basamak) geliyorsa
    // usse cevriliyor. Bu kosul YUK TASIYOR, olculdu: kosulsuz tuketilirse
    // `int x = 1e;` SESSIZCE derleniyor ve 0 basiyor, `print(y - 1e)` ise
    // `1e`yi 1.0 sayiyor — eksik us bir yazim hatasi degil, GECERLI bir
    // sayi oluyor ve tek kelime tani cikmiyor.
    // Regresyon: tests/silent_failure_probe.py "us: eksik us HATA vermeli".
    if (current_char_ == 'e' || current_char_ == 'E') {
        char sign = peek();
        bool ok = std::isdigit(static_cast<unsigned char>(sign)) ||
                  ((sign == '+' || sign == '-') &&
                   std::isdigit(static_cast<unsigned char>(peek(2))));
        if (ok) {
            is_float = true;   // us varsa sonuc her zaman float (`1e3` -> 1000.0)
            buffer += current_char_;
            advance();
            if (current_char_ == '+' || current_char_ == '-') {
                buffer += current_char_;
                advance();
            }
            while (std::isdigit(static_cast<unsigned char>(current_char_))) {
                buffer += current_char_;
                advance();
            }
        }
    }

    if (ident_start_char(current_char_))
        return number_suffix_error(start_line, start_column);

    return Token(is_float ? TOKEN_FLOAT_LITERAL : TOKEN_INT_LITERAL,
                 buffer, start_line, start_column);
}

Token Lexer::read_string() {
    int start_line = line_;
    int start_column = column_;
    std::string buffer;
    
    advance(); // Skip opening "
    
    while (current_char_ != '\0' && current_char_ != '"') {
        if (current_char_ == '\\') {
            advance();
            // Escape sequences
            switch (current_char_) {
                case 'n': buffer += '\n'; break;
                case 't': buffer += '\t'; break;
                case 'r': buffer += '\r'; break;
                case '\\': buffer += '\\'; break;
                case '"': buffer += '"'; break;
                case 'e': buffer += '\x1b'; break; // ESC — ANSI escape sequences
                case 'x': case 'X': {
                    // \xHH — up to two hex digits (e.g. "\x1b" == ESC).
                    int val = 0, cnt = 0;
                    while (cnt < 2) {
                        char p = peek();
                        int d;
                        if (p >= '0' && p <= '9') d = p - '0';
                        else if (p >= 'a' && p <= 'f') d = p - 'a' + 10;
                        else if (p >= 'A' && p <= 'F') d = p - 'A' + 10;
                        else break;
                        advance();
                        val = val * 16 + d;
                        cnt++;
                    }
                    if (cnt == 0) buffer += 'x';        // no hex digits: literal x
                    else buffer += static_cast<char>(val);
                    break;
                }
                case '0': case '1': case '2': case '3':
                case '4': case '5': case '6': case '7': {
                    // Octal \NNN — up to three octal digits (e.g. "\033" == ESC,
                    // "\0" == NUL). Prevents the old footgun where "\033" was
                    // read as NUL + "33", silently truncating ANSI strings.
                    int val = current_char_ - '0';
                    int cnt = 1;
                    while (cnt < 3) {
                        char p = peek();
                        if (p < '0' || p > '7') break;
                        advance();
                        val = val * 8 + (p - '0');
                        cnt++;
                    }
                    buffer += static_cast<char>(val & 0xFF);
                    break;
                }
                default: buffer += current_char_; break;
            }
        } else {
            buffer += current_char_;
        }
        advance();
    }
    
    if (current_char_ == '"') {
        advance(); // Skip closing "
    }
    
    return Token(TOKEN_STRING_LITERAL, buffer, start_line, start_column);
}

// Scan a t-string body and return its RAW inner template (the text between the
// outer quotes), with `{...}` interpolations left intact. The only cleverness
// here is finding the correct closing `"`: braces nest, and an inner string
// literal inside an interpolation (e.g. `{req["path"]}`) must not be mistaken
// for the terminator. The parser later splits this into literal + expression
// parts and desugars to string concatenation.
Token Lexer::read_tstring() {
    int start_line = line_;
    int start_column = column_;
    std::string buffer;

    advance(); // skip opening "

    int brace_depth = 0;
    while (current_char_ != '\0') {
        char c = current_char_;
        if (brace_depth == 0) {
            if (c == '"') { advance(); break; }   // end of the t-string
            if (c == '\\') {
                // Keep the escape pair verbatim; the parser processes escapes
                // for literal segments (\n, \", \{, ...).
                buffer += c; advance();
                if (current_char_ != '\0') { buffer += current_char_; advance(); }
                continue;
            }
            if (c == '{') { brace_depth = 1; }
            buffer += c; advance();
        } else {
            // Inside an interpolation expression.
            if (c == '{') { brace_depth++; buffer += c; advance(); continue; }
            if (c == '}') { brace_depth--; buffer += c; advance(); continue; }
            if (c == '"') {
                // Inner string literal — copy verbatim so its `}`/`"` don't
                // disturb brace tracking or terminate the t-string.
                buffer += c; advance();
                while (current_char_ != '\0' && current_char_ != '"') {
                    if (current_char_ == '\\') {
                        buffer += current_char_; advance();
                        if (current_char_ != '\0') { buffer += current_char_; advance(); }
                        continue;
                    }
                    buffer += current_char_; advance();
                }
                if (current_char_ == '"') { buffer += current_char_; advance(); }
                continue;
            }
            buffer += c; advance();
        }
    }

    return Token(TOKEN_TSTRING_LITERAL, buffer, start_line, start_column);
}

Token Lexer::read_identifier() {
    int start_line = line_;
    int start_column = column_;
    std::string buffer;
    
    while (current_char_ != '\0' &&
           (std::isalnum(current_char_) || current_char_ == '_' ||
            static_cast<unsigned char>(current_char_) > 127)) {
        buffer += current_char_;
        advance();
    }
    
    // Lookup keyword in the table
    const auto& keyword_map = get_keyword_map();
    auto it = keyword_map.find(buffer);
    TulparTokenType type = (it != keyword_map.end()) ? it->second : TOKEN_IDENTIFIER;
    
    return Token(type, buffer, start_line, start_column);
}

Token Lexer::next_token() {
    while (current_char_ != '\0') {
        // Skip whitespace
        if (std::isspace(current_char_)) {
            skip_whitespace();
            continue;
        }
        
        // Skip comments
        if (current_char_ == '/') {
            if (peek() == '/') {
                skip_comment();
                continue;
            } else if (peek() == '*') {
                skip_block_comment();
                continue;
            }
        }
        
        // Numbers
        if (std::isdigit(current_char_)) {
            return read_number();
        }
        
        // Interpolated t-string: t"...{expr}...". Must be checked before the
        // identifier branch so the leading `t` isn't lexed as an identifier.
        // Only `t` immediately followed by `"` triggers it (so `true`, `total`
        // stay normal identifiers).
        if (current_char_ == 't' && peek() == '"') {
            advance(); // consume 't'
            return read_tstring();
        }

        // Strings
        if (current_char_ == '"') {
            return read_string();
        }

        // Identifiers and keywords
        if (std::isalpha(current_char_) || current_char_ == '_' ||
            static_cast<unsigned char>(current_char_) > 127) {
            return read_identifier();
        }
        
        // Operators and symbols
        int start_line = line_;
        int start_column = column_;
        char ch = current_char_;
        char next_ch = peek();
        
        // Two-character operators
        if (ch == '+' && next_ch == '+') {
            advance(); advance();
            return Token(TOKEN_PLUS_PLUS, "++", start_line, start_column);
        }
        if (ch == '-' && next_ch == '-') {
            advance(); advance();
            return Token(TOKEN_MINUS_MINUS, "--", start_line, start_column);
        }
        if (ch == '+' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_PLUS_EQUAL, "+=", start_line, start_column);
        }
        if (ch == '-' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_MINUS_EQUAL, "-=", start_line, start_column);
        }
        if (ch == '*' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_MULTIPLY_EQUAL, "*=", start_line, start_column);
        }
        if (ch == '/' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_DIVIDE_EQUAL, "/=", start_line, start_column);
        }
        // `%=` otekiler (+= -= *= /=) varken YOKTU: `x %= 5` ayristirma
        // hatasi veriyordu. Tutarlilik icin eklendi.
        if (ch == '%' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_MODULO_EQUAL, "%=", start_line, start_column);
        }
        if (ch == '=' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_EQUAL, "==", start_line, start_column);
        }
        if (ch == '=' && next_ch == '>') {
            // Lambda head: `(int a, int b) => expr`. Parser
            // recognises this after a parenthesised parameter
            // list to build an AST_LAMBDA node. Codegen for
            // lambdas isn't wired up yet — current parser
            // surfaces a clear "lambdas not yet supported"
            // error so the syntax can land first and the
            // codegen + env-capture follow-ups stay scoped to
            // their own PRs.
            advance(); advance();
            return Token(TOKEN_FAT_ARROW, "=>", start_line, start_column);
        }
        if (ch == '!' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_NOT_EQUAL, "!=", start_line, start_column);
        }
        if (ch == '<' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_LESS_EQUAL, "<=", start_line, start_column);
        }
        if (ch == '>' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_GREATER_EQUAL, ">=", start_line, start_column);
        }
        if (ch == '&' && next_ch == '&') {
            advance(); advance();
            return Token(TOKEN_AND, "&&", start_line, start_column);
        }
        if (ch == '|' && next_ch == '|') {
            advance(); advance();
            return Token(TOKEN_OR, "||", start_line, start_column);
        }
        if (ch == '.' && next_ch == '.') {
            advance(); advance();
            return Token(TOKEN_DOTDOT, "..", start_line, start_column);
        }
        // Uc karakterli atamali kaydirma — `<<` / `>>`DEN ONCE sinanmali,
        // yoksa `x <<= 2` once `<<` sonra `=` olarak cikar ve ayristirma
        // hatasi verir.
        if (ch == '<' && next_ch == '<' && peek(2) == '=') {
            advance(); advance(); advance();
            return Token(TOKEN_SHIFT_LEFT_EQUAL, "<<=", start_line, start_column);
        }
        if (ch == '>' && next_ch == '>' && peek(2) == '=') {
            advance(); advance(); advance();
            return Token(TOKEN_SHIFT_RIGHT_EQUAL, ">>=", start_line, start_column);
        }
        // Atamali bit bicimleri. `&&`/`||` YUKARIDA yakalandigi icin
        // `&=` ile cakisma yok.
        if (ch == '&' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_BIT_AND_EQUAL, "&=", start_line, start_column);
        }
        if (ch == '|' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_BIT_OR_EQUAL, "|=", start_line, start_column);
        }
        if (ch == '^' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_BIT_XOR_EQUAL, "^=", start_line, start_column);
        }
        // Kaydirma. SIRA ONEMLI: `<=` / `>=` YUKARIDA sinandi, `<` / `>` ise
        // ASAGIDAKI tek karakter switch'inde. Ikisinin arasina girmezse
        // `a << 2` iki ayri `<` olarak cikar ve ayristirici "ifade bekleniyor"
        // der. `>>=` gibi atamali bicimler YOK (bkz. rapor: kapsam disi).
        if (ch == '<' && next_ch == '<') {
            advance(); advance();
            return Token(TOKEN_SHIFT_LEFT, "<<", start_line, start_column);
        }
        if (ch == '>' && next_ch == '>') {
            advance(); advance();
            return Token(TOKEN_SHIFT_RIGHT, ">>", start_line, start_column);
        }

        // Single-character operators
        advance();
        std::string value(1, ch);
        
        switch (ch) {
            case '+': return Token(TOKEN_PLUS, value, start_line, start_column);
            case '-': return Token(TOKEN_MINUS, value, start_line, start_column);
            case '*': return Token(TOKEN_MULTIPLY, value, start_line, start_column);
            case '/': return Token(TOKEN_DIVIDE, value, start_line, start_column);
            case '%': return Token(TOKEN_MODULO, value, start_line, start_column);
            case '=': return Token(TOKEN_ASSIGN, value, start_line, start_column);
            case '<': return Token(TOKEN_LESS, value, start_line, start_column);
            case '>': return Token(TOKEN_GREATER, value, start_line, start_column);
            case '!': return Token(TOKEN_BANG, value, start_line, start_column);
            case '(': return Token(TOKEN_LPAREN, value, start_line, start_column);
            case ')': return Token(TOKEN_RPAREN, value, start_line, start_column);
            case '{': return Token(TOKEN_LBRACE, value, start_line, start_column);
            case '}': return Token(TOKEN_RBRACE, value, start_line, start_column);
            case '[': return Token(TOKEN_LBRACKET, value, start_line, start_column);
            case ']': return Token(TOKEN_RBRACKET, value, start_line, start_column);
            case ';': return Token(TOKEN_SEMICOLON, value, start_line, start_column);
            case ',': return Token(TOKEN_COMMA, value, start_line, start_column);
            case ':': return Token(TOKEN_COLON, value, start_line, start_column);
            case '?': return Token(TOKEN_QUESTION, value, start_line, start_column);
            case '.': return Token(TOKEN_DOT, value, start_line, start_column);
            // `|` TEK token: hem match kolu ayraci hem bit VEYA. Ayrimi
            // ayristirici baglamdan yapiyor (bkz. lexer.hpp'deki not).
            case '|': return Token(TOKEN_PIPE, value, start_line, start_column);
            // Bit islemleri. `&&` ve `||` YUKARIDA yakalandi; buraya yalniz
            // TEK BASINA gelen `&` dusuyor. Once bu satirlar yoktu ve tek
            // basina `&` "Unknown character" hatasi veriyordu.
            case '&': return Token(TOKEN_BIT_AND, value, start_line, start_column);
            case '^': return Token(TOKEN_BIT_XOR, value, start_line, start_column);
            case '~': return Token(TOKEN_BIT_NOT, value, start_line, start_column);
            default:
                fprintf(stderr, tulpar::i18n::tr_for_en("Lexer Error: Unknown character '%c' at line %d, col %d\n"),
                        ch, start_line, start_column);
                return Token(TOKEN_ERROR, value, start_line, start_column);
        }
    }
    
    return Token(TOKEN_EOF, "", line_, column_);
}

// ============================================================================
// Legacy C API (for backward compatibility)
// ============================================================================

extern "C" {

Lexer* lexer_create(const char* source) {
    return new Lexer(std::string(source));
}

void lexer_free(Lexer* lexer) {
    delete lexer;
}

Token* lexer_next_token(Lexer* lexer) {
    Token tok = lexer->next_token();
    // Copy to heap for C API compatibility
    Token* heap_token = new Token(tok);
    return heap_token;
}

void token_free(Token* token) {
    delete token;
}

void token_print(Token* token) {
    token->print();
}

} // extern "C"
