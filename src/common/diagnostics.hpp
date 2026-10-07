#ifndef TULPAR_DIAGNOSTICS_HPP
#define TULPAR_DIAGNOSTICS_HPP

#include <string>
#include <vector>

namespace tulpar {

// Structured diagnostic record. Lines/columns are 1-based to match the
// existing parser/codegen rendering; LSP clients re-base to 0 when needed.
// `length` is the caret span (in source bytes) — 0 means "no specific
// caret token; highlight the whole line".
struct Diagnostic {
    int line;
    int column;
    int length;
    std::string severity;
    std::string message;
    std::string hint;
    // Tanının AİT OLDUĞU dosya (oyun geri bildirimi #4, 2026-10-08). Boş =
    // bilinmiyor (eski davranış: kök belge sayılır). İçe aktarılan bir
    // modülün hatası artık modülün yolunu taşır; LSP bunu kök belgeyle
    // karşılaştırıp modül tanısını içe aktaran satıra (anchor_line) koyar.
    std::string file;
    // Kök dosyada bu tanıya götüren üst düzey `import` satırı (1 tabanlı;
    // 0 = yok / tanı kök dosyanın kendisinde).
    int anchor_line = 0;
    // LSP'nin yeniden konumlandirdigi modul tanisinin ozgun yeri
    // (relatedInformation); bos = yok.
    std::string related_file;
    int related_line = 0, related_column = 0, related_length = 0;
};

// Process-global sink. When active, renderers in parser.cpp /
// llvm_backend.cpp push records here instead of writing to stderr.
// Single-threaded compilation is the only context that ever runs this,
// so a global is fine (matches the existing parser_set_diagnostic_context
// pattern).
void diag_sink_enable();
void diag_sink_disable();
bool diag_sink_active();
// `file` boşsa o an ayrıştırılan/sözcüklenen dosya (diag_file()) yazılır.
void diag_sink_push(int line, int column, int length,
                    const char *severity, const char *message,
                    const char *hint, const char *file = nullptr);

// O an sözcüklenen/ayrıştırılan kaynağın adı ve sessizlik kipi. Ayrıştırıcının
// parser_set_diagnostic_context / parser_set_quiet çağrıları buraya da yazar;
// sözcükleyici (lexer) hatalarını buradan okur — sözcükleyici ayrıştırıcıdan
// alt katman, onun durumunu göremez. Eskiden sözcükleyici hatası dosya adı
// taşımıyordu (`Lexer Error: ... at line 3`) ve typeinfer'in sessiz ön
// geçişinde de basılıp ikinci kopya veriyordu.
void diag_set_file(const char *file);
const char *diag_file();
void diag_set_quiet(bool quiet);
bool diag_quiet();
// Kök dosyadaki etkin üst düzey `import` satırı (modülün tanısı LSP'de oraya
// bağlanır). Kodgen, içe aktarılan modülü işlerken kurar; 0 = kök dosya.
void diag_set_anchor_line(int line);
int diag_anchor_line();
std::vector<Diagnostic> diag_sink_drain();

}  // namespace tulpar

#endif  // TULPAR_DIAGNOSTICS_HPP
