#ifndef AOT_PIPELINE_H
#define AOT_PIPELINE_H

#include "../parser/parser.h"

// AOT Compilation Result
typedef enum {
  AOT_OK,
  AOT_ERROR_PARSE,
  AOT_ERROR_CODEGEN,
  AOT_ERROR_EMIT,
  AOT_ERROR_LINK
} AOTResult;

// Compile Tulpar source to executable
// Returns AOT_OK on success, error code otherwise
AOTResult aot_compile(const char *source, const char *output_name);

// Compile and run (JIT-style, but AOT under the hood)
AOTResult aot_compile_and_run(const char *source);

#endif // AOT_PIPELINE_H
