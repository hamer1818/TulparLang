# Faz 1 Uygulama - Adım Adım

## Yapılması Gerekenler

### 1. Lexer ✅ TAMAMLANDI
- [x] Token'lar eklendi (&&, ||, !, ++, --, +=, -=, *=, /=)
- [x] Lexer parsing kodu eklendi

### 2. Parser - ŞİMDİ
- [ ] Expression parsing güncelle (&&, ||, ! için)
- [ ] Statement parsing güncelle (++, --, +=, -=, break, continue için)
- [ ] AST print güncelle

### 3. Interpreter - SONRA
- [ ] Mantıksal operatörleri değerlendir
- [ ] Increment/decrement çalıştır
- [ ] Compound assignment çalıştır
- [ ] Break/continue implement et
- [ ] Type conversion fonksiyonları ekle

## Zorluk Sırası

En kolay olan break/continue'den başlayalım:

1. Break/Continue (en kolay) - sadece flag set etmek
2. ++/-- (kolay) - basit assignment
3. +=, -=, *=, /= (kolay) - assignment + operation
4. &&, ||, ! (orta) - expression parsing priority ekle
5. Type conversion (kolay) - built-in fonksiyon

Şimdi kodları ekliyorum...



