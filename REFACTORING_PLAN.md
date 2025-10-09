# OLang Modüler Yapıya Geçiş Planı 🏗️

## 🎯 Amaç
Projeyi daha yönetilebilir, bakımı kolay ve ölçeklenebilir hale getirmek.

## 📁 Yeni Klasör Yapısı

```
OLang/
├── src/
│   ├── lexer/
│   │   ├── lexer.h          # Token türleri ve lexer interface
│   │   ├── lexer.c          # Lexer ana fonksiyonları
│   │   └── token.c          # Token helper fonksiyonları
│   │
│   ├── parser/
│   │   ├── parser.h         # AST türleri ve parser interface
│   │   ├── parser.c         # Parser ana fonksiyonları
│   │   ├── ast.c            # AST node fonksiyonları
│   │   └── ast_printer.c    # AST yazdırma (debug)
│   │
│   ├── interpreter/
│   │   ├── interpreter.h    # Interpreter interface
│   │   ├── interpreter.c    # Ana interpreter loop
│   │   ├── value.c          # Value type fonksiyonları
│   │   ├── symbol_table.c   # Symbol table yönetimi
│   │   └── builtins.c       # Built-in fonksiyonlar (print, input, etc)
│   │
│   ├── utils/
│   │   └── common.h         # Ortak tanımlamalar
│   │
│   └── main.c               # Ana program
│
├── build/                   # Derleme çıktıları
├── examples/                # Örnek programlar
├── Makefile                 # Build sistemi
└── README.md                # Döküman
```

## ✅ Yapılanlar

1. ✅ Klasör yapısı oluşturuldu
2. ✅ Dosyalar kopyalandı

## 🔄 Yapılacaklar

### Fase 1: Temel Yapı (ŞİMDİ)
- [ ] Include path'leri güncelle
- [ ] Makefile'ı yeniden yapılandır
- [ ] Build ve test et

### Fase 2: Lexer Modülü
- [ ] `lexer.c`'yi parçala:
  - `lexer.c` - Ana lexer loop
  - `token.c` - Token fonksiyonları

### Fase 3: Parser Modülü
- [ ] `parser.c`'yi parçala:
  - `parser.c` - Ana parser
  - `ast.c` - AST node management
  - `ast_printer.c` - Debug yazdırma

### Fase 4: Interpreter Modülü
- [ ] `interpreter.c`'yi parçala:
  - `interpreter.c` - Ana interpreter
  - `value.c` - Value operations
  - `symbol_table.c` - Değişken yönetimi
  - `builtins.c` - Built-in fonksiyonlar

### Fase 5: Utils
- [ ] Ortak tanımlamalar için `common.h` oluştur

## 📝 Include Path Güncellemeleri

### Eski:
```c
#include "lexer.h"
#include "parser.h"
#include "interpreter.h"
```

### Yeni:
```c
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"
```

## 🔧 Makefile Güncellemesi

```makefile
# Eski
SOURCES = $(wildcard $(SRC_DIR)/*.c)

# Yeni
SOURCES = $(wildcard $(SRC_DIR)/**/*.c) $(SRC_DIR)/main.c
```

## 💡 Avantajlar

1. **Daha İyi Organizasyon** - Her modül kendi klasöründe
2. **Kolay Bakım** - Kod parçaları küçük ve yönetilebilir
3. **Ölçeklenebilirlik** - Yeni özellikler kolayca eklenebilir
4. **Test Edilebilirlik** - Her modül ayrı test edilebilir
5. **Takım Çalışması** - Farklı kişiler farklı modüllerde çalışabilir

## ⚠️ Dikkat Edilmesi Gerekenler

1. Include path'leri doğru güncelle
2. Makefile'da tüm source dosyalarını ekle
3. Header guard'ları kontrol et
4. Build'i her adımda test et

---

**Adım adım ilerleyelim, her faz sonunda test edelim!** 🚀

