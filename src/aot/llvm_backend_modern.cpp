#include "llvm_backend_modern.hpp"
#include "../common/localization.hpp"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/IR/Verifier.h>
#include <iostream>

// ============================================================================
// Constructor & Initialization
// ============================================================================

LLVMBackend::LLVMBackend(const std::string& module_name)
    : context_(std::make_unique<llvm::LLVMContext>()),
      module_(std::make_unique<llvm::Module>(module_name, *context_)),
      builder_(std::make_unique<llvm::IRBuilder<>>(*context_)),
      current_function_(nullptr),
      current_scope_(std::make_unique<Scope>()),
      use_static_typing_(false),
      quiet_(false) {
    
    // Initialize types
    init_types();
    
    // Declare runtime functions
    declare_runtime_functions();
    
    if (!quiet_) {
        std::cout << "[AOT] LLVM Backend initialized (C++ API)" << std::endl;
    }
}

void LLVMBackend::init_types() {
    // Basic types
    int_type_ = llvm::Type::getInt64Ty(*context_);
    int32_type_ = llvm::Type::getInt32Ty(*context_);
    float_type_ = llvm::Type::getDoubleTy(*context_);
    bool_type_ = llvm::Type::getInt1Ty(*context_);
    void_type_ = llvm::Type::getVoidTy(*context_);
    ptr_type_ = llvm::Type::getInt8PtrTy(*context_);
    string_type_ = ptr_type_; // i8*
    
    // VM Types (struct definitions)
    // VMValue: { i32 type, [4 x i8] padding, i64 data }
    std::vector<llvm::Type*> vm_value_fields = {
        int32_type_,                          // type
        llvm::ArrayType::get(llvm::Type::getInt8Ty(*context_), 4), // padding
        int_type_                             // data (union as i64)
    };
    vm_value_type_ = llvm::StructType::create(*context_, vm_value_fields, "VMValue");
    
    // RetPair: { i64, i64 } for ABI-safe returns
    std::vector<llvm::Type*> ret_pair_fields = { int_type_, int_type_ };
    ret_pair_type_ = llvm::StructType::create(*context_, ret_pair_fields, "RetPair");
    
    // Opaque struct types for Obj and ObjString
    obj_type_ = llvm::StructType::create(*context_, "Obj");
    obj_string_type_ = llvm::StructType::create(*context_, "ObjString");
}

void LLVMBackend::declare_runtime_functions() {
    // printf: i32 printf(i8*, ...)
    llvm::FunctionType* printf_type = llvm::FunctionType::get(
        int32_type_,
        { string_type_ },
        true // varargs
    );
    func_printf_ = llvm::Function::Create(
        printf_type,
        llvm::Function::ExternalLinkage,
        "printf",
        module_.get()
    );
    
    // print_value: void print_value(VMValue*)
    llvm::FunctionType* print_value_type = llvm::FunctionType::get(
        void_type_,
        { ptr_type_ },
        false
    );
    func_print_value_ = llvm::Function::Create(
        print_value_type,
        llvm::Function::ExternalLinkage,
        "aot_print_value",
        module_.get()
    );
    
    // vm_alloc_string: ObjString* vm_alloc_string(VM*, i8*, i32)
    llvm::FunctionType* alloc_string_type = llvm::FunctionType::get(
        llvm::PointerType::get(obj_string_type_, 0),
        { ptr_type_, string_type_, int32_type_ },
        false
    );
    func_vm_alloc_string_ = llvm::Function::Create(
        alloc_string_type,
        llvm::Function::ExternalLinkage,
        "vm_alloc_string_aot",
        module_.get()
    );
    
    // vm_binary_op: void vm_binary_op(VM*, VMValue*, VMValue*, i32, VMValue*)
    llvm::FunctionType* binary_op_type = llvm::FunctionType::get(
        void_type_,
        { ptr_type_, ptr_type_, ptr_type_, int32_type_, ptr_type_ },
        false
    );
    func_vm_binary_op_ = llvm::Function::Create(
        binary_op_type,
        llvm::Function::ExternalLinkage,
        "vm_binary_op",
        module_.get()
    );
    
    // Array functions (simplified)
    // vm_allocate_array: ObjArray* vm_allocate_array(VM*)
    llvm::FunctionType* alloc_array_type = llvm::FunctionType::get(
        ptr_type_,
        { ptr_type_ },
        false
    );
    func_vm_allocate_array_ = llvm::Function::Create(
        alloc_array_type,
        llvm::Function::ExternalLinkage,
        "vm_allocate_array_aot_wrapper",
        module_.get()
    );
    
    // More runtime functions can be added as needed...
}

// ============================================================================
// Scope Management
// ============================================================================

void LLVMBackend::enter_scope() {
    auto new_scope = std::make_unique<Scope>(std::move(current_scope_));
    current_scope_ = std::move(new_scope);
}

void LLVMBackend::exit_scope() {
    if (current_scope_) {
        current_scope_ = current_scope_->release_parent();
    }
}

void LLVMBackend::add_local(const std::string& name, llvm::Value* value,
                             InferredType type, llvm::Value* native_value) {
    if (current_scope_) {
        current_scope_->add(name, LocalVariable(value, type, native_value));
    }
}

LocalVariable* LLVMBackend::get_local(const std::string& name) {
    if (current_scope_) {
        return current_scope_->find(name);
    }
    return nullptr;
}

llvm::AllocaInst* LLVMBackend::create_entry_block_alloca(
    llvm::Function* func, llvm::Type* type, const std::string& name) {
    
    llvm::IRBuilder<> tmp_builder(&func->getEntryBlock(),
                                   func->getEntryBlock().begin());
    return tmp_builder.CreateAlloca(type, nullptr, name);
}

// ============================================================================
// Main Compilation
// ============================================================================

void LLVMBackend::compile(const ASTNode& root) {
    try {
        codegen(root);
        
        // Verify module
        std::string error_str;
        llvm::raw_string_ostream error_stream(error_str);
        if (llvm::verifyModule(*module_, &error_stream)) {
            std::cerr << "Module verification failed:\n" << error_str << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << tulpar::i18n::tr_for_en("Compilation error: ") << e.what() << std::endl;
    }
}

llvm::Value* LLVMBackend::codegen(const ASTNode& node) {
    return std::visit([this](const auto& n) -> llvm::Value* {
        return this->codegen(n);
    }, node.value);
}

// ============================================================================
// Code Generation - Literals
// ============================================================================

llvm::Value* LLVMBackend::codegen(const IntLiteral& node) {
    return llvm::ConstantInt::get(*context_, llvm::APInt(64, node.value, true));
}

llvm::Value* LLVMBackend::codegen(const FloatLiteral& node) {
    return llvm::ConstantFP::get(*context_, llvm::APFloat(node.value));
}

llvm::Value* LLVMBackend::codegen(const StringLiteral& node) {
    return builder_->CreateGlobalStringPtr(node.value);
}

llvm::Value* LLVMBackend::codegen(const BoolLiteral& node) {
    return llvm::ConstantInt::get(bool_type_, node.value ? 1 : 0);
}

llvm::Value* LLVMBackend::codegen(const Identifier& node) {
    LocalVariable* var = get_local(node.name);
    if (!var) {
        throw std::runtime_error("Undefined variable: " + node.name);
    }
    
    // Load value if it's an alloca
    if (llvm::isa<llvm::AllocaInst>(var->value)) {
        return builder_->CreateLoad(int_type_, var->value, node.name);
    }
    
    return var->value;
}

// ============================================================================
// Code Generation - Expressions
// ============================================================================

llvm::Value* LLVMBackend::codegen(const BinaryOp& node) {
    llvm::Value* lhs = codegen(*node.left);
    llvm::Value* rhs = codegen(*node.right);
    
    if (!lhs || !rhs) {
        throw std::runtime_error("Failed to generate operands for binary op");
    }
    
    switch (node.op) {
        case TOKEN_PLUS:
            return builder_->CreateAdd(lhs, rhs, "addtmp");
        case TOKEN_MINUS:
            return builder_->CreateSub(lhs, rhs, "subtmp");
        case TOKEN_MULTIPLY:
            return builder_->CreateMul(lhs, rhs, "multmp");
        case TOKEN_DIVIDE:
            return builder_->CreateSDiv(lhs, rhs, "divtmp");
        case TOKEN_EQUAL:
            return builder_->CreateICmpEQ(lhs, rhs, "eqtmp");
        case TOKEN_NOT_EQUAL:
            return builder_->CreateICmpNE(lhs, rhs, "netmp");
        case TOKEN_LESS:
            return builder_->CreateICmpSLT(lhs, rhs, "lttmp");
        case TOKEN_GREATER:
            return builder_->CreateICmpSGT(lhs, rhs, "gttmp");
        case TOKEN_LESS_EQUAL:
            return builder_->CreateICmpSLE(lhs, rhs, "letmp");
        case TOKEN_GREATER_EQUAL:
            return builder_->CreateICmpSGE(lhs, rhs, "getmp");
        default:
            throw std::runtime_error("Unknown binary operator");
    }
}

llvm::Value* LLVMBackend::codegen(const UnaryOp& node) {
    llvm::Value* operand = codegen(*node.operand);
    
    switch (node.op) {
        case TOKEN_MINUS:
            return builder_->CreateNeg(operand, "negtmp");
        case TOKEN_BANG:
            return builder_->CreateNot(operand, "nottmp");
        default:
            throw std::runtime_error("Unknown unary operator");
    }
}

// ============================================================================
// Code Generation - Statements (stubs)
// ============================================================================

llvm::Value* LLVMBackend::codegen(const ArrayLiteral& node) {
    // TODO: Implement array literal code generation
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const ObjectLiteral& node) {
    // TODO: Implement object literal code generation
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const ArrayAccess& node) {
    // TODO: Implement array access code generation
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const FunctionCall& node) {
    llvm::Function* callee = module_->getFunction(node.name);
    if (!callee) {
        throw std::runtime_error("Unknown function: " + node.name);
    }
    
    std::vector<llvm::Value*> args;
    for (const auto& arg : node.arguments) {
        args.push_back(codegen(*arg));
    }
    
    return builder_->CreateCall(callee, args, "calltmp");
}

llvm::Value* LLVMBackend::codegen(const VariableDecl& node) {
    llvm::Type* var_type = datatype_to_llvm(node.data_type);
    llvm::AllocaInst* alloca = create_entry_block_alloca(
        current_function_, var_type, node.name
    );
    
    if (node.initializer) {
        llvm::Value* init_val = codegen(*node.initializer);
        builder_->CreateStore(init_val, alloca);
    }
    
    add_local(node.name, alloca);
    return alloca;
}

llvm::Value* LLVMBackend::codegen(const Assignment& node) {
    LocalVariable* var = get_local(node.name);
    if (!var) {
        throw std::runtime_error("Undefined variable: " + node.name);
    }
    
    llvm::Value* value = codegen(*node.value);
    builder_->CreateStore(value, var->value);
    return value;
}

llvm::Value* LLVMBackend::codegen(const CompoundAssign& node) {
    // TODO: Implement compound assignment
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const IncrementOp& node) {
    // TODO: Implement increment
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const DecrementOp& node) {
    // TODO: Implement decrement
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const FunctionDecl& node) {
    // Build parameter types
    std::vector<llvm::Type*> param_types;
    for (const auto& param : node.parameters) {
        param_types.push_back(datatype_to_llvm(param.type));
    }
    
    // Build function type
    llvm::Type* ret_type = datatype_to_llvm(node.return_type);
    llvm::FunctionType* func_type = llvm::FunctionType::get(
        ret_type, param_types, false
    );
    
    // Create function
    llvm::Function* func = llvm::Function::Create(
        func_type,
        llvm::Function::ExternalLinkage,
        node.name,
        module_.get()
    );
    
    // Set parameter names
    size_t idx = 0;
    for (auto& arg : func->args()) {
        arg.setName(node.parameters[idx++].name);
    }
    
    // Create entry block
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entry);
    
    // Enter new scope
    enter_scope();
    current_function_ = func;
    
    // Add parameters to scope
    for (auto& arg : func->args()) {
        llvm::AllocaInst* alloca = create_entry_block_alloca(
            func, arg.getType(), std::string(arg.getName())
        );
        builder_->CreateStore(&arg, alloca);
        add_local(std::string(arg.getName()), alloca);
    }
    
    // Generate function body
    codegen(*node.body);
    
    // Verify function
    if (llvm::verifyFunction(*func, &llvm::errs())) {
        func->eraseFromParent();
        throw std::runtime_error("Function verification failed");
    }
    
    // Exit scope
    exit_scope();
    current_function_ = nullptr;
    
    functions_[node.name] = func;
    return func;
}

llvm::Value* LLVMBackend::codegen(const IfStatement& node) {
    llvm::Value* cond = codegen(*node.condition);
    cond = builder_->CreateICmpNE(cond,
                                    llvm::ConstantInt::get(bool_type_, 0),
                                    "ifcond");
    
    llvm::Function* func = builder_->GetInsertBlock()->getParent();
    llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(*context_, "then", func);
    llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(*context_, "else");
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(*context_, "ifcont");
    
    builder_->CreateCondBr(cond, then_bb, else_bb);
    
    // Then block
    builder_->SetInsertPoint(then_bb);
    codegen(*node.then_branch);
    builder_->CreateBr(merge_bb);
    
    // Else block
    func->getBasicBlockList().push_back(else_bb);
    builder_->SetInsertPoint(else_bb);
    if (node.else_branch) {
        codegen(*node.else_branch);
    }
    builder_->CreateBr(merge_bb);
    
    // Merge block
    func->getBasicBlockList().push_back(merge_bb);
    builder_->SetInsertPoint(merge_bb);
    
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const WhileLoop& node) {
    llvm::Function* func = builder_->GetInsertBlock()->getParent();
    llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(*context_, "whilecond", func);
    llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(*context_, "whilebody");
    llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(*context_, "whileend");
    
    builder_->CreateBr(cond_bb);
    builder_->SetInsertPoint(cond_bb);
    
    llvm::Value* cond = codegen(*node.condition);
    cond = builder_->CreateICmpNE(cond,
                                    llvm::ConstantInt::get(bool_type_, 0),
                                    "whilecond");
    builder_->CreateCondBr(cond, body_bb, end_bb);
    
    func->getBasicBlockList().push_back(body_bb);
    builder_->SetInsertPoint(body_bb);
    codegen(*node.body);
    builder_->CreateBr(cond_bb);
    
    func->getBasicBlockList().push_back(end_bb);
    builder_->SetInsertPoint(end_bb);
    
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const ForLoop& node) {
    // TODO: Implement for loop
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const ForInLoop& node) {
    // TODO: Implement for-in loop
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const ReturnStatement& node) {
    if (node.value) {
        llvm::Value* ret_val = codegen(*node.value);
        return builder_->CreateRet(ret_val);
    }
    return builder_->CreateRetVoid();
}

llvm::Value* LLVMBackend::codegen(const BreakStatement& node) {
    // TODO: Implement break
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const ContinueStatement& node) {
    // TODO: Implement continue
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const Block& node) {
    llvm::Value* last = nullptr;
    for (const auto& stmt : node.statements) {
        last = codegen(*stmt);
    }
    return last;
}

llvm::Value* LLVMBackend::codegen(const Program& node) {
    llvm::Value* last = nullptr;
    for (const auto& stmt : node.statements) {
        last = codegen(*stmt);
    }
    return last;
}

llvm::Value* LLVMBackend::codegen(const TryCatch& node) {
    // TODO: Implement try-catch
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const ThrowStatement& node) {
    // TODO: Implement throw
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const ImportStatement& node) {
    // TODO: Implement import
    return nullptr;
}

llvm::Value* LLVMBackend::codegen(const TypeDecl& node) {
    // TODO: Implement type declaration
    return nullptr;
}

// ============================================================================
// Type Conversion Helpers
// ============================================================================

llvm::Type* LLVMBackend::datatype_to_llvm(DataType type) {
    switch (type) {
        case TYPE_INT: return int_type_;
        case TYPE_FLOAT: return float_type_;
        case TYPE_BOOL: return bool_type_;
        case TYPE_STRING: return string_type_;
        case TYPE_VOID: return void_type_;
        default: return vm_value_type_; // Use VMValue for complex types
    }
}

InferredType LLVMBackend::datatype_to_inferred(DataType type) {
    switch (type) {
        case TYPE_INT: return InferredType::Int;
        case TYPE_FLOAT: return InferredType::Float;
        case TYPE_BOOL: return InferredType::Bool;
        case TYPE_STRING: return InferredType::String;
        case TYPE_ARRAY:
        case TYPE_ARRAY_INT:
        case TYPE_ARRAY_FLOAT:
        case TYPE_ARRAY_STR:
        case TYPE_ARRAY_BOOL:
        case TYPE_ARRAY_JSON: return InferredType::Array;
        default: return InferredType::Unknown;
    }
}

// ============================================================================
// Optimization & Output
// ============================================================================

void LLVMBackend::optimize() {
    // TODO: Add optimization passes
    if (!quiet_) {
        std::cout << "[AOT] Running optimization passes..." << std::endl;
    }
}

bool LLVMBackend::emit_object_file(const std::string& filename) {
    // TODO: Implement object file emission
    if (!quiet_) {
        std::cout << "[AOT] Emitting object file: " << filename << std::endl;
    }
    return true;
}

bool LLVMBackend::emit_ir_file(const std::string& filename) {
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
    
    if (ec) {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }
    
    module_->print(dest, nullptr);
    
    if (!quiet_) {
        std::cout << "[AOT] IR file written: " << filename << std::endl;
    }
    
    return true;
}

// ============================================================================
// Legacy C API (stub implementations)
// ============================================================================

extern "C" {

LLVMBackend_C* llvm_backend_create(const char* module_name) {
    // TODO: Create C API wrapper
    return nullptr;
}

void llvm_backend_destroy(LLVMBackend_C* backend) {
    // TODO: Implement
}

void llvm_backend_compile(LLVMBackend_C* backend, ASTNode_C* node) {
    // TODO: Implement
}

void llvm_backend_optimize(LLVMBackend_C* backend) {
    // TODO: Implement
}

int llvm_backend_emit_object(LLVMBackend_C* backend, const char* filename) {
    // TODO: Implement
    return 0;
}

int llvm_backend_emit_ir_file(LLVMBackend_C* backend, const char* filename) {
    // TODO: Implement
    return 0;
}

} // extern "C"
