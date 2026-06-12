#ifndef AOT_PIPELINE_H
#define AOT_PIPELINE_H

#include "../parser/parser.hpp"

// AOT Compilation Result
typedef enum {
  AOT_OK,
  AOT_ERROR_PARSE,
  AOT_ERROR_CODEGEN,
  AOT_ERROR_EMIT,
  AOT_ERROR_LINK
} AOTResult;

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
