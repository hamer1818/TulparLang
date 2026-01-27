#include "lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Yardımcı fonksiyonlar
static void lexer_advance(Lexer *lexer) {
  lexer->position++;
  lexer->column++;

  size_t source_length = strlen(lexer->source);
  if ((size_t)lexer->position < source_length) {
    lexer->current_char = lexer->source[lexer->position];
  } else {
    lexer->current_char = '\0';
  }
}

static char lexer_peek(Lexer *lexer) {
  size_t source_length = strlen(lexer->source);
  size_t peek_pos = (size_t)lexer->position + 1;
  if (peek_pos < source_length) {
    return lexer->source[peek_pos];
  }
  return '\0';
}

static void lexer_skip_whitespace(Lexer *lexer) {
  while (lexer->current_char != '\0' && isspace(lexer->current_char)) {
    if (lexer->current_char == '\n') {
      lexer->line++;
      lexer->column = 0;
    }
    lexer_advance(lexer);
  }
}

static void lexer_skip_comment(Lexer *lexer) {
  if (lexer->current_char == '/' && lexer_peek(lexer) == '/') {
    // Tek satır yorum
    while (lexer->current_char != '\0' && lexer->current_char != '\n') {
      lexer_advance(lexer);
    }
  }
}

static void lexer_skip_block_comment(Lexer *lexer) {
  if (lexer->current_char == '/' && lexer_peek(lexer) == '*') {
    // Blok yorum: /* ... */
    int start_line = lexer->line;
    int start_col = lexer->column;
    lexer_advance(lexer); // '/'
    lexer_advance(lexer); // '*'
    while (lexer->current_char != '\0') {
      if (lexer->current_char == '*' && lexer_peek(lexer) == '/') {
        lexer_advance(lexer); // '*'
        lexer_advance(lexer); // '/'
        return;
      }
      if (lexer->current_char == '\n') {
        lexer->line++;
        lexer->column = 0;
      }
      lexer_advance(lexer);
    }
    // Kapanış bulunamadı
    fprintf(stderr,
            "Lexer Error: Block comment not terminated (started at line %d, "
            "col %d)\n",
            start_line, start_col);
  }
}

// Token oluşturma
static Token *token_create(TulparTokenType type, char *value, int line,
                           int column) {
  Token *token = (Token *)malloc(sizeof(Token));
  token->type = type;
  token->value = value ? strdup(value) : NULL;
  token->line = line;
  token->column = column;
  return token;
}

// Sayı okuma
static Token *lexer_read_number(Lexer *lexer) {
  int start_line = lexer->line;
  int start_column = lexer->column;
  char buffer[256];
  int i = 0;
  int is_float = 0;

  while (lexer->current_char != '\0' &&
         (isdigit(lexer->current_char) || lexer->current_char == '.')) {
    if (lexer->current_char == '.') {
      if (is_float)
        break; // İkinci nokta hata
      is_float = 1;
    }
    buffer[i++] = lexer->current_char;
    lexer_advance(lexer);
  }
  buffer[i] = '\0';

  TulparTokenType type = is_float ? TOKEN_FLOAT_LITERAL : TOKEN_INT_LITERAL;
  return token_create(type, buffer, start_line, start_column);
}

// String okuma
static Token *lexer_read_string(Lexer *lexer) {
  int start_line = lexer->line;
  int start_column = lexer->column;
  char buffer[1024];
  int i = 0;

  lexer_advance(lexer); // " karakterini atla

  while (lexer->current_char != '\0' && lexer->current_char != '"') {
    // Escape sequence desteği
    if (lexer->current_char == '\\') {
      lexer_advance(lexer); // \ karakterini atla

      switch (lexer->current_char) {
      case 'n': // Yeni satır
        buffer[i++] = '\n';
        break;
      case 't': // Tab
        buffer[i++] = '\t';
        break;
      case 'r': // Carriage return
        buffer[i++] = '\r';
        break;
      case '\\': // Backslash
        buffer[i++] = '\\';
        break;
      case '"': // Çift tırnak
        buffer[i++] = '"';
        break;
      case '0': // Null karakter
        buffer[i++] = '\0';
        break;
      default: // Bilinmeyen escape, olduğu gibi ekle
        buffer[i++] = '\\';
        buffer[i++] = lexer->current_char;
        break;
      }
      lexer_advance(lexer);
    } else {
      buffer[i++] = lexer->current_char;
      lexer_advance(lexer);
    }
  }

  if (lexer->current_char == '"') {
    lexer_advance(lexer); // Kapatan " karakterini atla
  }

  buffer[i] = '\0';
  return token_create(TOKEN_STRING_LITERAL, buffer, start_line, start_column);
}

// Identifier veya anahtar kelime okuma
static Token *lexer_read_identifier(Lexer *lexer) {
  int start_line = lexer->line;
  int start_column = lexer->column;
  char buffer[256];
  int i = 0;

  // UTF-8 desteği için: ASCII harfler, rakamlar, alt çizgi ve UTF-8 karakterler
  // İlk karakter rakam olamaz, onu parser_next_token'da kontrol ediyoruz
  while (lexer->current_char != '\0') {
    unsigned char c = (unsigned char)lexer->current_char;

    // ASCII: harf, rakam veya alt çizgi
    if (isalnum(c) || c == '_') {
      buffer[i++] = lexer->current_char;
      lexer_advance(lexer);
    }
    // UTF-8 multi-byte karakterler (0x80-0xFF arası başlayan)
    // Türkçe: ç(195,167) ğ(196,159) ı(196,177) ö(195,182) ş(197,159) ü(195,188)
    else if (c >= 0x80) {
      // UTF-8 multi-byte karakter, olduğu gibi al
      buffer[i++] = lexer->current_char;
      lexer_advance(lexer);
    } else {
      break;
    }

    // Buffer overflow kontrolü
    if (i >= 255)
      break;
  }
  buffer[i] = '\0';

  // Anahtar kelime kontrolü
  TulparTokenType type = TOKEN_IDENTIFIER;

  if (strcmp(buffer, "int") == 0)
    type = TOKEN_INT_TYPE;
  else if (strcmp(buffer, "float") == 0)
    type = TOKEN_FLOAT_TYPE;
  else if (strcmp(buffer, "str") == 0)
    type = TOKEN_STR_TYPE;
  else if (strcmp(buffer, "bool") == 0)
    type = TOKEN_BOOL_TYPE;
  else if (strcmp(buffer, "array") == 0)
    type = TOKEN_ARRAY_TYPE;
  else if (strcmp(buffer, "arrayInt") == 0)
    type = TOKEN_ARRAY_INT;
  else if (strcmp(buffer, "arrayFloat") == 0)
    type = TOKEN_ARRAY_FLOAT;
  else if (strcmp(buffer, "arrayStr") == 0)
    type = TOKEN_ARRAY_STR;
  else if (strcmp(buffer, "arrayBool") == 0)
    type = TOKEN_ARRAY_BOOL;
  else if (strcmp(buffer, "arrayJson") == 0)
    type = TOKEN_ARRAY_JSON;
  else if (strcmp(buffer, "func") == 0)
    type = TOKEN_FUNC;
  else if (strcmp(buffer, "type") == 0)
    type = TOKEN_TYPE_KW;
  else if (strcmp(buffer, "return") == 0)
    type = TOKEN_RETURN;
  else if (strcmp(buffer, "import") == 0)
    type = TOKEN_IMPORT;
  else if (strcmp(buffer, "if") == 0)
    type = TOKEN_IF;
  else if (strcmp(buffer, "else") == 0)
    type = TOKEN_ELSE;
  else if (strcmp(buffer, "while") == 0)
    type = TOKEN_WHILE;
  else if (strcmp(buffer, "for") == 0)
    type = TOKEN_FOR;
  else if (strcmp(buffer, "in") == 0)
    type = TOKEN_IN;
  else if (strcmp(buffer, "break") == 0)
    type = TOKEN_BREAK;
  else if (strcmp(buffer, "continue") == 0)
    type = TOKEN_CONTINUE;
  else if (strcmp(buffer, "try") == 0)
    type = TOKEN_TRY;
  else if (strcmp(buffer, "catch") == 0)
    type = TOKEN_CATCH;
  else if (strcmp(buffer, "finally") == 0)
    type = TOKEN_FINALLY;
  else if (strcmp(buffer, "throw") == 0)
    type = TOKEN_THROW;
  else if (strcmp(buffer, "true") == 0)
    type = TOKEN_TRUE;
  else if (strcmp(buffer, "false") == 0)
    type = TOKEN_FALSE;
  else if (strcmp(buffer, "move") == 0)
    type = TOKEN_MOVE;
  else if (strcmp(buffer, "var") == 0)
    type = TOKEN_VAR;

  return token_create(type, buffer, start_line, start_column);
}

// Lexer oluşturma
Lexer *lexer_create(const char *source) {
  Lexer *lexer = (Lexer *)malloc(sizeof(Lexer));
  lexer->source = strdup(source);
  lexer->position = 0;
  lexer->line = 1;
  lexer->column = 0;
  lexer->current_char = source[0];
  return lexer;
}

// Lexer bellekten silme
void lexer_free(Lexer *lexer) {
  if (lexer) {
    free(lexer->source);
    free(lexer);
  }
}

// Token bellekten silme
void token_free(Token *token) {
  if (token) {
    if (token->value)
      free(token->value);
    free(token);
  }
}

// Bir sonraki token'ı al
Token *lexer_next_token(Lexer *lexer) {
  while (lexer->current_char != '\0') {
    int start_line = lexer->line;
    int start_column = lexer->column;

    // Boşlukları atla
    if (isspace(lexer->current_char)) {
      lexer_skip_whitespace(lexer);
      continue;
    }

    // Yorumları atla
    if (lexer->current_char == '/') {
      if (lexer_peek(lexer) == '/') {
        lexer_skip_comment(lexer);
        continue;
      }
      if (lexer_peek(lexer) == '*') {
        lexer_skip_block_comment(lexer);
        continue;
      }
    }

    // Sayılar
    if (isdigit(lexer->current_char)) {
      return lexer_read_number(lexer);
    }

    // String'ler
    if (lexer->current_char == '"') {
      return lexer_read_string(lexer);
    }

    // Identifier'lar ve anahtar kelimeler
    // UTF-8 desteği: ASCII harfler, alt çizgi veya 0x80+ (UTF-8 multi-byte)
    unsigned char uc = (unsigned char)lexer->current_char;
    if (isalpha(uc) || uc == '_' || uc >= 0x80) {
      return lexer_read_identifier(lexer);
    }

    // Tek karakterli tokenlar
    char c = lexer->current_char;
    lexer_advance(lexer);

    switch (c) {
    case '+':
      if (lexer->current_char == '+') {
        lexer_advance(lexer);
        return token_create(TOKEN_PLUS_PLUS, "++", start_line, start_column);
      }
      if (lexer->current_char == '=') {
        lexer_advance(lexer);
        return token_create(TOKEN_PLUS_EQUAL, "+=", start_line, start_column);
      }
      return token_create(TOKEN_PLUS, "+", start_line, start_column);
    case '-':
      if (lexer->current_char == '-') {
        lexer_advance(lexer);
        return token_create(TOKEN_MINUS_MINUS, "--", start_line, start_column);
      }
      if (lexer->current_char == '=') {
        lexer_advance(lexer);
        return token_create(TOKEN_MINUS_EQUAL, "-=", start_line, start_column);
      }
      return token_create(TOKEN_MINUS, "-", start_line, start_column);
    case '*':
      if (lexer->current_char == '=') {
        lexer_advance(lexer);
        return token_create(TOKEN_MULTIPLY_EQUAL, "*=", start_line,
                            start_column);
      }
      return token_create(TOKEN_MULTIPLY, "*", start_line, start_column);
    case '/':
      if (lexer->current_char == '=') {
        lexer_advance(lexer);
        return token_create(TOKEN_DIVIDE_EQUAL, "/=", start_line, start_column);
      }
      return token_create(TOKEN_DIVIDE, "/", start_line, start_column);
    case '(':
      return token_create(TOKEN_LPAREN, "(", start_line, start_column);
    case ')':
      return token_create(TOKEN_RPAREN, ")", start_line, start_column);
    case '{':
      return token_create(TOKEN_LBRACE, "{", start_line, start_column);
    case '}':
      return token_create(TOKEN_RBRACE, "}", start_line, start_column);
    case '[':
      return token_create(TOKEN_LBRACKET, "[", start_line, start_column);
    case ']':
      return token_create(TOKEN_RBRACKET, "]", start_line, start_column);
    case ';':
      return token_create(TOKEN_SEMICOLON, ";", start_line, start_column);
    case ',':
      return token_create(TOKEN_COMMA, ",", start_line, start_column);
    case ':':
      return token_create(TOKEN_COLON, ":", start_line, start_column);
    case '.':
      return token_create(TOKEN_DOT, ".", start_line, start_column);
    case '=':
      if (lexer->current_char == '=') {
        lexer_advance(lexer);
        return token_create(TOKEN_EQUAL, "==", start_line, start_column);
      }
      return token_create(TOKEN_ASSIGN, "=", start_line, start_column);
    case '!':
      if (lexer->current_char == '=') {
        lexer_advance(lexer);
        return token_create(TOKEN_NOT_EQUAL, "!=", start_line, start_column);
      }
      return token_create(TOKEN_BANG, "!", start_line, start_column);
    case '&':
      if (lexer->current_char == '&') {
        lexer_advance(lexer);
        return token_create(TOKEN_AND, "&&", start_line, start_column);
      }
      break;
    case '|':
      if (lexer->current_char == '|') {
        lexer_advance(lexer);
        return token_create(TOKEN_OR, "||", start_line, start_column);
      }
      break;
    case '<':
      if (lexer->current_char == '=') {
        lexer_advance(lexer);
        return token_create(TOKEN_LESS_EQUAL, "<=", start_line, start_column);
      }
      return token_create(TOKEN_LESS, "<", start_line, start_column);
    case '>':
      if (lexer->current_char == '=') {
        lexer_advance(lexer);
        return token_create(TOKEN_GREATER_EQUAL, ">=", start_line,
                            start_column);
      }
      return token_create(TOKEN_GREATER, ">", start_line, start_column);
    }

    return token_create(TOKEN_ERROR, NULL, start_line, start_column);
  }

  return token_create(TOKEN_EOF, NULL, lexer->line, lexer->column);
}

// Token yazdırma (debug için)
void token_print(Token *token) {
  printf("Token(type=%d, value='%s', line=%d, col=%d)\n", token->type,
         token->value ? token->value : "NULL", token->line, token->column);
}