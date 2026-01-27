# ============================================
# Tulpar Embedded Libraries - CMake Generator
# ============================================
# This script reads lib/*.tpr files and generates
# embedded_libs.h at build time automatically.
# ============================================

# Function to escape file content for C string literal
function(escape_for_c_string INPUT_STRING OUTPUT_VAR)
    # Read the string
    set(RESULT "${INPUT_STRING}")
    
    # Escape backslashes first (must be first!)
    string(REPLACE "\\" "\\\\" RESULT "${RESULT}")
    
    # Escape double quotes
    string(REPLACE "\"" "\\\"" RESULT "${RESULT}")
    
    # Escape newlines - replace with \n" newline "
    string(REPLACE "\n" "\\n\"\n    \"" RESULT "${RESULT}")
    
    # Escape carriage returns
    string(REPLACE "\r" "\\r" RESULT "${RESULT}")
    
    # Escape tabs
    string(REPLACE "\t" "\\t" RESULT "${RESULT}")
    
    # Wrap in quotes
    set(RESULT "\"${RESULT}\"")
    
    # Return
    set(${OUTPUT_VAR} "${RESULT}" PARENT_SCOPE)
endfunction()

# Function to embed a library file
function(embed_library LIB_NAME LIB_FILE OUTPUT_VAR)
    set(LIB_PATH "${CMAKE_SOURCE_DIR}/lib/${LIB_FILE}")
    
    if(EXISTS "${LIB_PATH}")
        file(READ "${LIB_PATH}" LIB_CONTENT)
        escape_for_c_string("${LIB_CONTENT}" ESCAPED_CONTENT)
        set(${OUTPUT_VAR} "${ESCAPED_CONTENT}" PARENT_SCOPE)
        message(STATUS "Embedded library: ${LIB_NAME} (${LIB_FILE})")
    else()
        message(WARNING "Library file not found: ${LIB_PATH}")
        set(${OUTPUT_VAR} "\"// Library not found: ${LIB_FILE}\"" PARENT_SCOPE)
    endif()
endfunction()

# Embed all libraries
embed_library("wings" "wings.tpr" EMBEDDED_WINGS_CONTENT)
embed_library("router" "router.tpr" EMBEDDED_ROUTER_CONTENT)
embed_library("http_utils" "http_utils.tpr" EMBEDDED_HTTP_UTILS_CONTENT)

# Generate the header file from template
configure_file(
    "${CMAKE_SOURCE_DIR}/src/embedded_libs.h.in"
    "${CMAKE_SOURCE_DIR}/src/embedded_libs.h"
    @ONLY
)

message(STATUS "Generated: src/embedded_libs.h")
