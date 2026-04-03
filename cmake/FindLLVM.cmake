# FindLLVM.cmake - Find LLVM on Windows when LLVMConfig.cmake is not available
# This is a fallback module for Windows builds where LLVM is installed via Chocolatey

if(WIN32)
    # Try to find LLVM installation directory
    set(LLVM_POSSIBLE_PATHS
        "C:/Program Files/LLVM"
        "C:/LLVM"
        "$ENV{LLVM_DIR}"
        "$ENV{LLVM_CMAKE_DIR}"
    )
    
    foreach(LLVM_PATH ${LLVM_POSSIBLE_PATHS})
        if(EXISTS "${LLVM_PATH}/include/llvm")
            set(LLVM_ROOT "${LLVM_PATH}")
            break()
        endif()
    endforeach()
    
    if(NOT LLVM_ROOT)
        message(FATAL_ERROR "Could not find LLVM installation. Please set LLVM_DIR environment variable.")
    endif()
    
    # Set LLVM paths
    set(LLVM_INCLUDE_DIRS "${LLVM_ROOT}/include")
    set(LLVM_LIBRARY_DIRS "${LLVM_ROOT}/lib")
    
    # Find LLVM version
    if(EXISTS "${LLVM_INCLUDE_DIRS}/llvm/Config/llvm-config.h")
        file(READ "${LLVM_INCLUDE_DIRS}/llvm/Config/llvm-config.h" LLVM_CONFIG_H)
        string(REGEX MATCH "#define LLVM_VERSION_MAJOR ([0-9]+)" _ ${LLVM_CONFIG_H})
        set(LLVM_VERSION_MAJOR ${CMAKE_MATCH_1})
        string(REGEX MATCH "#define LLVM_VERSION_MINOR ([0-9]+)" _ ${LLVM_CONFIG_H})
        set(LLVM_VERSION_MINOR ${CMAKE_MATCH_1})
        string(REGEX MATCH "#define LLVM_VERSION_PATCH ([0-9]+)" _ ${LLVM_CONFIG_H})
        set(LLVM_VERSION_PATCH ${CMAKE_MATCH_1})
        set(LLVM_PACKAGE_VERSION "${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}")
    else()
        set(LLVM_PACKAGE_VERSION "Unknown")
    endif()
    
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
