# FindLLVM.cmake - Find LLVM on Windows when LLVMConfig.cmake is not available
# This is a fallback module for Windows builds where LLVM is installed via Chocolatey

if(WIN32)
    # First check if LLVM_DIR was passed as a CMake variable
    if(LLVM_DIR AND EXISTS "${LLVM_DIR}")
        set(LLVM_ROOT "${LLVM_DIR}")
        message(STATUS "FindLLVM: Using LLVM_DIR from CMake variable: ${LLVM_ROOT}")
    else()
        # Try to find LLVM installation directory from environment or standard paths
        set(LLVM_POSSIBLE_PATHS
            "$ENV{LLVM_DIR}"
            "$ENV{LLVM_CMAKE_DIR}"
            "C:/Program Files/LLVM"
            "C:/LLVM"
        )
        
        # Check multiple possible indicators that LLVM is installed
        foreach(LLVM_PATH ${LLVM_POSSIBLE_PATHS})
            if(LLVM_PATH AND EXISTS "${LLVM_PATH}")
                # Check for any of these indicators
                if(EXISTS "${LLVM_PATH}/include/llvm" OR 
                   EXISTS "${LLVM_PATH}/include/llvm-c" OR
                   EXISTS "${LLVM_PATH}/lib" OR
                   EXISTS "${LLVM_PATH}/bin/clang.exe")
                    set(LLVM_ROOT "${LLVM_PATH}")
                    message(STATUS "FindLLVM: Found LLVM at ${LLVM_ROOT}")
                    break()
                endif()
            endif()
        endforeach()
        
        if(NOT LLVM_ROOT)
            # Fallback: just use C:/Program Files/LLVM if it exists
            if(EXISTS "C:/Program Files/LLVM")
                set(LLVM_ROOT "C:/Program Files/LLVM")
                message(STATUS "FindLLVM: Using default LLVM path: ${LLVM_ROOT}")
            else()
                message(STATUS "FindLLVM: Searched paths:")
                foreach(LLVM_PATH ${LLVM_POSSIBLE_PATHS})
                    message(STATUS "  - ${LLVM_PATH}")
                endforeach()
                message(FATAL_ERROR "Could not find LLVM installation. Please set LLVM_DIR environment variable.")
            endif()
        endif()
    endif()
    
    # Set LLVM paths
    set(LLVM_INCLUDE_DIRS "${LLVM_ROOT}/include")
    set(LLVM_LIBRARY_DIRS "${LLVM_ROOT}/lib")
    
    message(STATUS "FindLLVM: LLVM_INCLUDE_DIRS = ${LLVM_INCLUDE_DIRS}")
    message(STATUS "FindLLVM: LLVM_LIBRARY_DIRS = ${LLVM_LIBRARY_DIRS}")
    
    # List what's in the lib directory for debugging
    if(EXISTS "${LLVM_LIBRARY_DIRS}")
        file(GLOB LLVM_LIB_FILES "${LLVM_LIBRARY_DIRS}/*.lib")
        list(LENGTH LLVM_LIB_FILES LLVM_LIB_COUNT)
        message(STATUS "FindLLVM: Found ${LLVM_LIB_COUNT} .lib files in ${LLVM_LIBRARY_DIRS}")
    endif()
    
    # Find LLVM version from llvm-config.h or llvm/Config/llvm-config.h
    set(LLVM_CONFIG_PATHS
        "${LLVM_INCLUDE_DIRS}/llvm/Config/llvm-config.h"
        "${LLVM_INCLUDE_DIRS}/llvm-c/llvm-config.h"
    )
    
    set(LLVM_PACKAGE_VERSION "18.0.0")  # Default assumption
    foreach(CONFIG_PATH ${LLVM_CONFIG_PATHS})
        if(EXISTS "${CONFIG_PATH}")
            file(READ "${CONFIG_PATH}" LLVM_CONFIG_H)
            string(REGEX MATCH "#define LLVM_VERSION_MAJOR ([0-9]+)" _ ${LLVM_CONFIG_H})
            if(CMAKE_MATCH_1)
                set(LLVM_VERSION_MAJOR ${CMAKE_MATCH_1})
                string(REGEX MATCH "#define LLVM_VERSION_MINOR ([0-9]+)" _ ${LLVM_CONFIG_H})
                set(LLVM_VERSION_MINOR ${CMAKE_MATCH_1})
                string(REGEX MATCH "#define LLVM_VERSION_PATCH ([0-9]+)" _ ${LLVM_CONFIG_H})
                set(LLVM_VERSION_PATCH ${CMAKE_MATCH_1})
                set(LLVM_PACKAGE_VERSION "${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}")
                message(STATUS "FindLLVM: Detected LLVM version ${LLVM_PACKAGE_VERSION}")
                break()
            endif()
        endif()
    endforeach()
    
    # Find required LLVM libraries
    set(LLVM_REQUIRED_LIBS
        LLVMCore LLVMSupport LLVMIRReader LLVMBitReader LLVMBinaryFormat
        LLVMX86AsmParser LLVMX86CodeGen LLVMX86Desc LLVMX86Info LLVMX86Disassembler
        LLVMPasses LLVMAsmParser LLVMAsmPrinter
        LLVMTarget LLVMMC LLVMMCParser LLVMCodeGen LLVMScalarOpts LLVMInstCombine
        LLVMTransformUtils LLVMAnalysis LLVMObject LLVMRemarks LLVMBitstreamReader
        LLVMProfileData LLVMDebugInfoDWARF LLVMDebugInfoCodeView LLVMTextAPI
        LLVMDemangle
    )
    
    set(LLVM_LIBRARIES "")
    foreach(LIB ${LLVM_REQUIRED_LIBS})
        find_library(${LIB}_PATH NAMES ${LIB} PATHS "${LLVM_LIBRARY_DIRS}" NO_DEFAULT_PATH)
        if(${LIB}_PATH)
            list(APPEND LLVM_LIBRARIES ${${LIB}_PATH})
        endif()
    endforeach()
    
    # Set LLVM_FOUND
    if(LLVM_INCLUDE_DIRS AND LLVM_LIBRARY_DIRS)
        set(LLVM_FOUND TRUE)
        set(LLVM_DIR "${LLVM_ROOT}")
        
        # Define llvm_map_components_to_libnames function for compatibility
        function(llvm_map_components_to_libnames out_libs)
            set(components ${ARGN})
            set(libs "")
            foreach(comp ${components})
                string(TOUPPER ${comp} comp_upper)
                string(REPLACE "-" "" comp_upper ${comp_upper})
                
                # Map component names to library names
                if(comp STREQUAL "support")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMSupport.lib")
                elseif(comp STREQUAL "core")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMCore.lib")
                elseif(comp STREQUAL "irreader")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMIRReader.lib")
                elseif(comp STREQUAL "x86asmparser")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMX86AsmParser.lib")
                elseif(comp STREQUAL "x86codegen")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMX86CodeGen.lib")
                elseif(comp STREQUAL "x86desc")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMX86Desc.lib")
                elseif(comp STREQUAL "x86info")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMX86Info.lib")
                elseif(comp STREQUAL "passes")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMPasses.lib")
                elseif(comp STREQUAL "asmparser")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMAsmParser.lib")
                elseif(comp STREQUAL "asmprinter")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMAsmPrinter.lib")
                elseif(comp STREQUAL "target")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMTarget.lib")
                elseif(comp STREQUAL "mc")
                    list(APPEND libs "${LLVM_LIBRARY_DIRS}/LLVMMC.lib")
                endif()
            endforeach()
            set(${out_libs} ${libs} PARENT_SCOPE)
        endfunction()
    else()
        set(LLVM_FOUND FALSE)
    endif()
else()
    set(LLVM_FOUND FALSE)
endif()

# Handle find_package arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LLVM
    REQUIRED_VARS LLVM_INCLUDE_DIRS LLVM_LIBRARY_DIRS
    VERSION_VAR LLVM_PACKAGE_VERSION
)
