#ifndef AOT_PIPELINE_H
#define AOT_PIPELINE_H

#include "../parser/parser.hpp"

// AOT Compilation Result
typedef enum {
  AOT_OK,
  AOT_ERROR_PARSE,
  AOT_ERROR_CODEGEN,
  AOT_ERROR_EMIT,
  AOT_ERROR_LINK,
  // Compile + link succeeded and the program RAN, but it exited non-zero.
  // Distinct from AOT_ERROR_LINK so the driver propagates the program's
  // failure without the misleading "compile/link failed" banner (e.g. a
  // server killed by Ctrl+C, or a program that called exit(1) itself).
  AOT_RAN_NONZERO
} AOTResult;

// Web hedefi anahtarı (`tulpar build --target=web`): 1 verilirse sonraki
// aot_compile* çağrıları wasm32-unknown-emscripten objesi üretir ve em++
// ile wasm/dist arşivlerine (build_tame_web.sh) linkler — çıktı
// <out>.html + .js + .wasm. VMValue çağrı ABI'sini de değiştirir (sret;
// bkz. llvm_values.cpp), o yüzden codegen başlamadan önce çağrılmalıdır.
void aot_set_target_web(int enable);
// `tulpar build --target=android`: emit arm64-v8a + x86_64 objects, link
// them with the NDK toolchain into per-ABI libtulpargame.so files and
// write an APK staging dir (<out>_apk/). Packaging into a signed .apk is
// android/package_apk.sh's job.
void aot_set_target_android(int enable);
// `tulpar build --apk`: after the android staging dir is written, also run
// android/package_apk.sh (aapt2 + zipalign + apksigner) so a single command
// yields an installable signed <out>.apk. Implies the android target.
void aot_set_android_apk(int enable);
// `tulpar build --aab`: staging'in ardından android/package_aab.sh koşar →
// Play Store'a yüklenebilir imzalı <out>.aab (bundletool). Android hedefini ima
// eder; --apk yerine geçer (ikisi verilirse --aab kazanır).
void aot_set_android_aab(int enable);

// Compile Tulpar source to executable (verbose mode).
// Returns AOT_OK on success, error code otherwise.
// `source_filename` is optional — when provided, codegen errors include
// the file path in the diagnostic header (`--> path/file.tpr:42`).
AOTResult aot_compile(const char *source, const char *output_name);
AOTResult aot_compile_with_filename(const char *source,
                                    const char *output_name,
                                    const char *source_filename);

// `emit_debug_info != 0` requests an AOT build that retains debug
// symbols: clang is invoked with `-g`, the optimiser is held at -O0
// (otherwise inlined functions confuse `gdb`/`lldb` line stepping),
// and the LLVMBackend's emit_debug_info slot is set so later PRs can
// drop in `LLVMDIBuilder` metadata without changing the call chain.
// This is the entry point for `tulpar build --debug` (Plan 07 PR 1);
// the full DWARF / CodeView emission lands in Plan 07 PR 2-3.
AOTResult aot_compile_with_filename_debug(const char *source,
                                          const char *output_name,
                                          const char *source_filename,
                                          int emit_debug_info);

// Compile and run (JIT-style, but AOT under the hood)
AOTResult aot_compile_and_run(const char *source);

// Silent compile and run - no [AOT] messages, temp binary, auto-cleanup.
// Returns AOT_OK on success. Used as default execution mode.
AOTResult aot_compile_and_run_silent(const char *source);
AOTResult aot_compile_and_run_silent_with_filename(const char *source,
                                                   const char *source_filename);

// Check-only: parse + codegen the source for diagnostic purposes only,
// without optimising, emitting an object file, or linking. Intended for
// LSP / editor integrations that need fast turnaround on syntax + semantic
// errors. Caller is expected to enable the diagnostic sink (see
// `tulpar/diagnostics.hpp`) before calling and drain it afterwards.
AOTResult aot_check_only(const char *source, const char *source_filename);

// Same as `aot_check_only` but also fills a `DocumentIndex` with function
// signatures and leading comments before the AST is freed. Used by the
// LSP server for hover / completion. Declared as `void *` here so the
// pipeline header can stay free of LSP includes — callers cast to
// `tulpar::DocumentIndex *`.
AOTResult aot_check_and_index(const char *source, const char *source_filename,
                              void *out_index);

#endif // AOT_PIPELINE_H
