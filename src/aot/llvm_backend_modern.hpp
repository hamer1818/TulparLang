#ifndef TULPAR_LLVM_BACKEND_HPP
#define TULPAR_LLVM_BACKEND_HPP

#include "../parser/parser.hpp"
#include "../parser/ast_nodes.hpp"
#include "../parser/ast_visitor.hpp"

// Modern LLVM C++ API headers
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

// Type inference enumeration
enum class InferredType {
    Unknown = 0,
    Int,
    Float,
    Bool,
    String,
    Array
};

// ============================================================================
// Modern C++ Scope Management
// ============================================================================

struct LocalVariable {
    llvm::Value* value;
    InferredType known_type;
    llvm::Value* native_value;
    
    LocalVariable(llvm::Value* v = nullptr,
                  InferredType t = InferredType::Unknown,
                  llvm::Value* nv = nullptr)
        : value(v), known_type(t), native_value(nv) {}
};

class Scope {
private:
    std::unordered_map<std::string, LocalVariable> variables_;
    std::unique_ptr<Scope> parent_;

public:
    Scope() = default;
    explicit Scope(std::unique_ptr<Scope> parent)
        : parent_(std::move(parent)) {}
    
    void add(const std::string& name, const LocalVariable& var) {
        variables_[name] = var;
    }
    
    LocalVariable* find(const std::string& name) {
        auto it = variables_.find(name);
        if (it != variables_.end()) {
            return &it->second;
        }
        if (parent_) {
            return parent_->find(name);
        }
        return nullptr;
    }
    
    std::unique_ptr<Scope> release_parent() {
        return std::move(parent_);
    }
};

// ============================================================================
// Function Registry
// ============================================================================

struct FunctionInfo {
    llvm::FunctionType* type;
    InferredType return_type;
    std::vector<InferredType> param_types;
    
    FunctionInfo(llvm::FunctionType* t = nullptr,
                 InferredType ret = InferredType::Unknown)
        : type(t), return_type(ret) {}
};

// ============================================================================
// LLVM Backend Class
// ============================================================================

class LLVMBackend {
private:
    // Core LLVM components (modern C++ API)
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;
    
    // Type cache (LLVM C++ types)
    llvm::Type* int_type_;
    llvm::Type* int32_type_;
    llvm::Type* float_type_;
    llvm::Type* bool_type_;
    llvm::Type* void_type_;
    llvm::Type* ptr_type_;
    llvm::Type* string_type_;
    
    // VM Types
    llvm::StructType* vm_value_type_;
    llvm::StructType* ret_pair_type_;
    llvm::StructType* obj_type_;
    llvm::StructType* obj_string_type_;
    
    // Runtime Functions (subset - most important ones)
    llvm::Function* func_printf_;
    llvm::Function* func_print_value_;
    llvm::Function* func_vm_alloc_string_;
    llvm::Function* func_vm_binary_op_;
    llvm::Function* func_vm_allocate_array_;
    llvm::Function* func_vm_array_push_;
    llvm::Function* func_vm_array_get_;
    llvm::Function* func_vm_array_set_;
    
    // Scope management
    llvm::Function* current_function_;
    std::unique_ptr<Scope> current_scope_;
    
    // Function registry
    std::unordered_map<std::string, llvm::Function*> functions_;
    std::unordered_map<std::string, FunctionInfo> function_info_;
    
    // Imported files tracking
    std::vector<std::string> imported_files_;
    
    // Config flags
    bool use_static_typing_;
    bool quiet_;
    
    // Helper methods
    void init_types();
    void declare_runtime_functions();
    void enter_scope();
    void exit_scope();
    void add_local(const std::string& name, llvm::Value* value,
                   InferredType type = InferredType::Unknown,
                   llvm::Value* native_value = nullptr);
    LocalVariable* get_local(const std::string& name);
    
    llvm::AllocaInst* create_entry_block_alloca(llvm::Function* func,
                                                  llvm::Type* type,
                                                  const std::string& name);

public:
    // Constructor & Destructor
    explicit LLVMBackend(const std::string& module_name);
    ~LLVMBackend() = default;
    
    // Main compilation methods
    void compile(const ASTNode& root);
    void optimize();
    bool emit_object_file(const std::string& filename);
    bool emit_ir_file(const std::string& filename);
    
    // Code generation methods (visitor-style)
    llvm::Value* codegen(const IntLiteral& node);
    llvm::Value* codegen(const FloatLiteral& node);
    llvm::Value* codegen(const StringLiteral& node);
    llvm::Value* codegen(const BoolLiteral& node);
    llvm::Value* codegen(const Identifier& node);
    llvm::Value* codegen(const BinaryOp& node);
    llvm::Value* codegen(const UnaryOp& node);
    llvm::Value* codegen(const ArrayLiteral& node);
    llvm::Value* codegen(const ObjectLiteral& node);
    llvm::Value* codegen(const ArrayAccess& node);
    llvm::Value* codegen(const FunctionCall& node);
    llvm::Value* codegen(const VariableDecl& node);
    llvm::Value* codegen(const Assignment& node);
    llvm::Value* codegen(const CompoundAssign& node);
    llvm::Value* codegen(const IncrementOp& node);
    llvm::Value* codegen(const DecrementOp& node);
    llvm::Value* codegen(const FunctionDecl& node);
    llvm::Value* codegen(const IfStatement& node);
    llvm::Value* codegen(const WhileLoop& node);
    llvm::Value* codegen(const ForLoop& node);
    llvm::Value* codegen(const ForInLoop& node);
    llvm::Value* codegen(const ReturnStatement& node);
    llvm::Value* codegen(const BreakStatement& node);
    llvm::Value* codegen(const ContinueStatement& node);
    llvm::Value* codegen(const Block& node);
    llvm::Value* codegen(const Program& node);
    llvm::Value* codegen(const TryCatch& node);
    llvm::Value* codegen(const ThrowStatement& node);
    llvm::Value* codegen(const ImportStatement& node);
    llvm::Value* codegen(const TypeDecl& node);
    
    // Generic codegen using std::visit
    llvm::Value* codegen(const ASTNode& node);
    
    // Type conversion helpers
    llvm::Type* datatype_to_llvm(DataType type);
    InferredType datatype_to_inferred(DataType type);
    
    // Configuration
    void enable_static_typing() { use_static_typing_ = true; }
    void set_quiet(bool quiet) { quiet_ = quiet; }
    
    // Getters
    llvm::LLVMContext& context() { return *context_; }
    llvm::Module& module() { return *module_; }
    llvm::IRBuilder<>& builder() { return *builder_; }
};

// ============================================================================
// Legacy C API (for backward compatibility)
// ============================================================================

// Old C API types (kept for compatibility with existing code)
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

typedef struct {
  LLVMContextRef context;
  LLVMModuleRef module;
  LLVMBuilderRef builder;
  LLVMTypeRef int_type;
  LLVMTypeRef int32_type;
  LLVMTypeRef float_type;
  LLVMTypeRef bool_type;
  LLVMTypeRef void_type;
  LLVMTypeRef ptr_type;
  LLVMTypeRef string_type;
  LLVMTypeRef vm_value_type;
  LLVMValueRef func_printf;
  // ... (truncated for brevity - full struct kept in legacy code)
  int quiet;
} LLVMBackend_C;

extern "C" {
    LLVMBackend_C* llvm_backend_create(const char* module_name);
    void llvm_backend_destroy(LLVMBackend_C* backend);
    void llvm_backend_compile(LLVMBackend_C* backend, ASTNode_C* node);
    void llvm_backend_optimize(LLVMBackend_C* backend);
    int llvm_backend_emit_object(LLVMBackend_C* backend, const char* filename);
    int llvm_backend_emit_ir_file(LLVMBackend_C* backend, const char* filename);
}

#endif // TULPAR_LLVM_BACKEND_HPP
