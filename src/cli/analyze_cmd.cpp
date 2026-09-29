// `tulpar analyze <file.tpr>` (K157). Ayrinti: analyze_cmd.hpp.
//
// NEDEN: iki bilgi vardi ama ikisi de ancak soruldugunda ve ayri ayri
// gorunuyordu. `@no_alloc` (K041) yalniz ISARETLI fonksiyonu denetliyor —
// "hangi fonksiyonlarim ayiriyor?" sorusu icin her birine isaret koyup
// derlemek gerekiyordu. Hizli yol ipuclari (K167) yalniz
// `TULPAR_PERF_HINTS=1` ile, derlemenin stderr'ine karisarak basiliyordu.
// Bu komut ikisini ayni kurallarla (kopya yok: typeinfer'in denetimi ve
// backend'in ipucu kodu) tek raporda veriyor.
//
// KAPSAM, bilerek soyleniyor: ayirma kurali BEYAZ LISTE — "ayirmasiz"
// kurala gore kanitli, "ayiriyor" en fazla temkinli (taninmayan yerlesik
// ayirabilir sayilir). Calisma zamaninda olculmedi; dogrulugu
// `@no_alloc`un kendi fiksturlerine (tests/typeinfer/*no_alloc*) dayaniyor,
// raporla denetimin AYNI sonucu verdigini tests/analyze_smoke.sh olcuyor.

#include "analyze_cmd.hpp"

#include "../aot/aot_pipeline.hpp"
#include "../common/localization.hpp"
#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include "../typeinfer/typeinfer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define AN_DUP _dup
#define AN_DUP2 _dup2
#define AN_FILENO _fileno
#define AN_CLOSE _close
#else
#include <unistd.h>
#define AN_DUP dup
#define AN_DUP2 dup2
#define AN_FILENO fileno
#define AN_CLOSE close
#endif

namespace tulpar {

namespace {

// Backend ipuclarini stderr'e yaziyor; raporun icine almak icin kodegen
// suresince stderr'i gecici dosyaya cevir.
std::string capture_stderr_during_check(const std::string &source, const char *path,
                                        bool *codegen_ok) {
  std::fflush(stderr);
  FILE *tmp = std::tmpfile();
  std::string named;
  if (!tmp) {
    // Windows'ta tmpfile() kok dizine yazmaya calisip dusebiliyor; TEMP'e
    // adli dosyayla dus.
    const char *d = std::getenv("TMPDIR");
    if (!d || !*d) d = std::getenv("TEMP");
    if (!d || !*d) d = std::getenv("TMP");
    if (!d || !*d) d = ".";
    named = std::string(d) + "/tulpar_analyze_" + std::to_string((long)std::time(nullptr)) + ".txt";
    tmp = std::fopen(named.c_str(), "w+b");
  }
  if (!tmp) {
    *codegen_ok = aot_check_only(source.c_str(), path) == AOT_OK;
    return "";
  }
  int saved = AN_DUP(AN_FILENO(stderr));
  AN_DUP2(AN_FILENO(tmp), AN_FILENO(stderr));
  *codegen_ok = aot_check_only(source.c_str(), path) == AOT_OK;
  std::fflush(stderr);
  AN_DUP2(saved, AN_FILENO(stderr));
  AN_CLOSE(saved);
  std::string out;
  std::rewind(tmp);
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, tmp)) > 0) out.append(buf, n);
  std::fclose(tmp);
  if (!named.empty()) std::remove(named.c_str());
  return out;
}

int count_hints(const std::string &text) {
  int n = 0;
  const char *keys[] = {"performans ipucu:", "performance hint:"};
  for (const char *k : keys) {
    for (size_t p = text.find(k); p != std::string::npos; p = text.find(k, p + 1)) n++;
  }
  return n;
}

}  // namespace

int analyze_cmd_main(int argc, char **argv) {
  const char *path = nullptr;
  for (int i = 2; i < argc; i++) {
    if (argv[i][0] == '-') {
      std::fprintf(stderr, "tulpar analyze: unknown flag '%s'\n", argv[i]);
      return 2;
    }
    if (path) {
      std::fprintf(stderr, "tulpar analyze: only one file at a time\n");
      return 2;
    }
    path = argv[i];
  }
  if (!path) {
    std::fprintf(stderr, "Usage: tulpar analyze <file.tpr>\n");
    return 2;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "tulpar analyze: cannot open '%s'\n", path);
    return 2;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string source = ss.str();
  in.close();

  Lexer lexer(source);
  std::vector<Token> tokens;
  while (true) {
    Token tok = lexer.next_token();
    bool eof = tok.type() == TOKEN_EOF;
    tokens.push_back(std::move(tok));
    if (eof) break;
  }
  parser_set_diagnostic_context(source.c_str(), path);
  std::unique_ptr<ASTNode> ast;
  try {
    Parser parser(std::move(tokens));
    ast = parser.parse();
  } catch (const std::exception &) {
    return 2;
  }
  if (!ast || parser_get_error_count() > 0) {
    std::fprintf(stderr, "tulpar analyze: parse error(s) in %s\n", path);
    return 2;
  }

  // 1) Ayirma: typeinfer (tanilarini stderr'e kendisi basar), sonra rapor.
  TypeInferContext *ctx = typeinfer_create();
  typeinfer_program(ctx, ast.get());
  const int type_issues = typeinfer_has_errors(ctx) ? ctx->error_count : 0;
  std::vector<TypeinferAllocRow> rows = typeinfer_alloc_report(ctx, ast.get());
  typeinfer_destroy(ctx);

  std::printf("tulpar analyze: %s\n\n", path);
  std::printf("%s\n", i18n::tr_en(
      "ayirma (@no_alloc kurali, gecisli; \"ayirmasiz\" kurala gore kanitli, "
      "\"AYIRIYOR\" temkinli olabilir):",
      "allocation (@no_alloc rule, transitive; \"no-alloc\" is proven by the rule, "
      "\"ALLOCATES\" may be conservative):"));
  size_t w = 8;
  for (const auto &r : rows) w = r.name.size() > w ? r.name.size() : w;
  int clean = 0, dirty = 0;
  for (const auto &r : rows) {
    const char *mark = r.no_alloc_marked ? " @no_alloc" : "";
    if (r.reason.empty()) {
      clean++;
      std::printf("  %-*s  %s %-5d %s%s\n", (int)w, r.name.c_str(),
                  i18n::tr_en("satir", "line"), r.line,
                  i18n::tr_en("ayirmasiz", "no-alloc"), mark);
    } else {
      dirty++;
      std::printf("  %-*s  %s %-5d %s — %s%s\n", (int)w, r.name.c_str(),
                  i18n::tr_en("satir", "line"), r.line,
                  i18n::tr_en("AYIRIYOR", "ALLOCATES"), r.reason.c_str(), mark);
    }
  }
  if (rows.empty())
    std::printf("  %s\n", i18n::tr_en("(ust duzey fonksiyon yok)", "(no top-level functions)"));

  // 2) Hizli yol: backend'in K167 ipuclari (derleme/link yok, yalniz kodegen).
#ifdef _WIN32
  _putenv("TULPAR_PERF_HINTS=1");
#else
  setenv("TULPAR_PERF_HINTS", "1", 1);
#endif
  bool codegen_ok = true;
  const std::string hints = capture_stderr_during_check(source, path, &codegen_ok);
  const int hint_n = count_hints(hints);
  std::printf("\n%s\n", i18n::tr_en("hizli yol (kanitli dizi erisimi, TULPAR_PERF_HINTS):",
                                    "fast path (proven array access, TULPAR_PERF_HINTS):"));
  if (hints.empty()) {
    std::printf("  %s\n", i18n::tr_en("ipucu yok", "no hints"));
  } else {
    std::istringstream hs(hints);
    std::string ln;
    while (std::getline(hs, ln)) std::printf("  %s\n", ln.c_str());
  }

  std::printf("\n%s: %d %s — %d %s, %d %s; %d %s; %d %s%s\n",
              i18n::tr_en("ozet", "summary"), (int)rows.size(),
              i18n::tr_en("fonksiyon", "functions"), clean,
              i18n::tr_en("ayirmasiz", "no-alloc"), dirty,
              i18n::tr_en("ayiriyor", "allocate"), hint_n,
              i18n::tr_en("hizli yol ipucu", "fast-path hints"), type_issues,
              i18n::tr_en("tip sorunu", "type issues"),
              codegen_ok ? "" : i18n::tr_en(" (kodegen hatasi — yukariya bakin)",
                                            " (codegen error — see above)"));
  if (!codegen_ok) return 2;
  return type_issues > 0 ? 1 : 0;
}

}  // namespace tulpar
