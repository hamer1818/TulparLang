#ifndef TULPAR_AST_VISITOR_HPP
#define TULPAR_AST_VISITOR_HPP

#include "ast_nodes.hpp"
#include <variant>

// ============================================================================
// AST Visitor Base Template
// ============================================================================

template<typename ReturnType>
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;
    
    // Literal visitors
    virtual ReturnType visit(const IntLiteral& node) = 0;
    virtual ReturnType visit(const FloatLiteral& node) = 0;
    virtual ReturnType visit(const StringLiteral& node) = 0;
    virtual ReturnType visit(const BoolLiteral& node) = 0;
    virtual ReturnType visit(const Identifier& node) = 0;
    
    // Expression visitors
    virtual ReturnType visit(const BinaryOp& node) = 0;
    virtual ReturnType visit(const UnaryOp& node) = 0;
    virtual ReturnType visit(const ArrayLiteral& node) = 0;
    virtual ReturnType visit(const ObjectLiteral& node) = 0;
    virtual ReturnType visit(const ArrayAccess& node) = 0;
    virtual ReturnType visit(const FunctionCall& node) = 0;
    
    // Statement visitors
    virtual ReturnType visit(const VariableDecl& node) = 0;
    virtual ReturnType visit(const Assignment& node) = 0;
    virtual ReturnType visit(const CompoundAssign& node) = 0;
    virtual ReturnType visit(const IncrementOp& node) = 0;
    virtual ReturnType visit(const DecrementOp& node) = 0;
    virtual ReturnType visit(const FunctionDecl& node) = 0;
    virtual ReturnType visit(const IfStatement& node) = 0;
    virtual ReturnType visit(const WhileLoop& node) = 0;
    virtual ReturnType visit(const ForLoop& node) = 0;
    virtual ReturnType visit(const ForInLoop& node) = 0;
    virtual ReturnType visit(const ReturnStatement& node) = 0;
    virtual ReturnType visit(const BreakStatement& node) = 0;
    virtual ReturnType visit(const ContinueStatement& node) = 0;
    virtual ReturnType visit(const Block& node) = 0;
    virtual ReturnType visit(const Program& node) = 0;
    virtual ReturnType visit(const TryCatch& node) = 0;
    virtual ReturnType visit(const ThrowStatement& node) = 0;
    virtual ReturnType visit(const ImportStatement& node) = 0;
    virtual ReturnType visit(const TypeDecl& node) = 0;
    
    // Apply visitor to any ASTNode
    ReturnType apply(const ASTNode& node) {
        return std::visit([this](const auto& arg) -> ReturnType {
            return this->visit(arg);
        }, node.value);
    }
    
    // Apply visitor to unique_ptr<ASTNode>
    ReturnType apply(const std::unique_ptr<ASTNode>& node) {
        if (!node) {
            throw std::runtime_error("Null AST node in visitor");
        }
        return apply(*node);
    }
};

// ============================================================================
// Helper: Print AST Visitor
// ============================================================================

class ASTPrintVisitor : public ASTVisitor<void> {
private:
    int indent_level_;
    
    void print_indent() {
        for (int i = 0; i < indent_level_; ++i) {
            printf("  ");
        }
    }
    
public:
    ASTPrintVisitor() : indent_level_(0) {}
    
    void visit(const IntLiteral& node) override {
        print_indent();
        printf("IntLiteral: %lld\n", node.value);
    }
    
    void visit(const FloatLiteral& node) override {
        print_indent();
        printf("FloatLiteral: %f\n", node.value);
    }
    
    void visit(const StringLiteral& node) override {
        print_indent();
        printf("StringLiteral: \"%s\"\n", node.value.c_str());
    }
    
    void visit(const BoolLiteral& node) override {
        print_indent();
        printf("BoolLiteral: %s\n", node.value ? "true" : "false");
    }
    
    void visit(const Identifier& node) override {
        print_indent();
        printf("Identifier: %s\n", node.name.c_str());
    }
    
    void visit(const BinaryOp& node) override {
        print_indent();
        printf("BinaryOp:\n");
        indent_level_++;
        print_indent();
        printf("Left:\n");
        indent_level_++;
        apply(node.left);
        indent_level_--;
        print_indent();
        printf("Right:\n");
        indent_level_++;
        apply(node.right);
        indent_level_--;
        indent_level_--;
    }
    
    void visit(const UnaryOp& node) override {
        print_indent();
        printf("UnaryOp:\n");
        indent_level_++;
        apply(node.operand);
        indent_level_--;
    }
    
    void visit(const ArrayLiteral& node) override {
        print_indent();
        printf("ArrayLiteral (%zu elements)\n", node.elements.size());
        indent_level_++;
        for (const auto& elem : node.elements) {
            apply(elem);
        }
        indent_level_--;
    }
    
    void visit(const ObjectLiteral& node) override {
        print_indent();
        printf("ObjectLiteral (%zu fields)\n", node.fields.size());
        indent_level_++;
        for (const auto& [key, value] : node.fields) {
            print_indent();
            printf("Key: %s\n", key.c_str());
            indent_level_++;
            apply(value);
            indent_level_--;
        }
        indent_level_--;
    }
    
    void visit(const ArrayAccess& node) override {
        print_indent();
        printf("ArrayAccess:\n");
        indent_level_++;
        apply(node.object);
        apply(node.index);
        indent_level_--;
    }
    
    void visit(const FunctionCall& node) override {
        print_indent();
        printf("FunctionCall: %s\n", node.name.c_str());
        indent_level_++;
        for (const auto& arg : node.arguments) {
            apply(arg);
        }
        indent_level_--;
    }
    
    void visit(const VariableDecl& node) override {
        print_indent();
        printf("VariableDecl: %s\n", node.name.c_str());
        if (node.initializer) {
            indent_level_++;
            apply(node.initializer);
            indent_level_--;
        }
    }
    
    void visit(const Assignment& node) override {
        print_indent();
        printf("Assignment: %s\n", node.name.c_str());
        indent_level_++;
        apply(node.value);
        indent_level_--;
    }
    
    void visit(const CompoundAssign& node) override {
        print_indent();
        printf("CompoundAssign: %s\n", node.name.c_str());
        indent_level_++;
        apply(node.value);
        indent_level_--;
    }
    
    void visit(const IncrementOp& node) override {
        print_indent();
        printf("IncrementOp: %s\n", node.name.c_str());
    }
    
    void visit(const DecrementOp& node) override {
        print_indent();
        printf("DecrementOp: %s\n", node.name.c_str());
    }
    
    void visit(const FunctionDecl& node) override {
        print_indent();
        printf("FunctionDecl: %s (%zu params)\n", node.name.c_str(), node.parameters.size());
        indent_level_++;
        apply(node.body);
        indent_level_--;
    }
    
    void visit(const IfStatement& node) override {
        print_indent();
        printf("IfStatement:\n");
        indent_level_++;
        print_indent();
        printf("Condition:\n");
        indent_level_++;
        apply(node.condition);
        indent_level_--;
        print_indent();
        printf("Then:\n");
        indent_level_++;
        apply(node.then_branch);
        indent_level_--;
        if (node.else_branch) {
            print_indent();
            printf("Else:\n");
            indent_level_++;
            apply(node.else_branch);
            indent_level_--;
        }
        indent_level_--;
    }
    
    void visit(const WhileLoop& node) override {
        print_indent();
        printf("WhileLoop:\n");
        indent_level_++;
        apply(node.condition);
        apply(node.body);
        indent_level_--;
    }
    
    void visit(const ForLoop& node) override {
        print_indent();
        printf("ForLoop:\n");
        indent_level_++;
        if (node.init) apply(node.init);
        if (node.condition) apply(node.condition);
        if (node.increment) apply(node.increment);
        apply(node.body);
        indent_level_--;
    }
    
    void visit(const ForInLoop& node) override {
        print_indent();
        printf("ForInLoop: %s\n", node.variable.c_str());
        indent_level_++;
        apply(node.iterable);
        apply(node.body);
        indent_level_--;
    }
    
    void visit(const ReturnStatement& node) override {
        print_indent();
        printf("ReturnStatement:\n");
        if (node.value) {
            indent_level_++;
            apply(node.value);
            indent_level_--;
        }
    }
    
    void visit(const BreakStatement& node) override {
        print_indent();
        printf("BreakStatement\n");
    }
    
    void visit(const ContinueStatement& node) override {
        print_indent();
        printf("ContinueStatement\n");
    }
    
    void visit(const Block& node) override {
        print_indent();
        printf("Block (%zu statements)\n", node.statements.size());
        indent_level_++;
        for (const auto& stmt : node.statements) {
            apply(stmt);
        }
        indent_level_--;
    }
    
    void visit(const Program& node) override {
        print_indent();
        printf("Program (%zu statements)\n", node.statements.size());
        indent_level_++;
        for (const auto& stmt : node.statements) {
            apply(stmt);
        }
        indent_level_--;
    }
    
    void visit(const TryCatch& node) override {
        print_indent();
        printf("TryCatch:\n");
        indent_level_++;
        apply(node.try_block);
        apply(node.catch_block);
        if (node.finally_block) apply(node.finally_block);
        indent_level_--;
    }
    
    void visit(const ThrowStatement& node) override {
        print_indent();
        printf("ThrowStatement:\n");
        indent_level_++;
        apply(node.expression);
        indent_level_--;
    }
    
    void visit(const ImportStatement& node) override {
        print_indent();
        printf("ImportStatement: %s\n", node.path.c_str());
    }
    
    void visit(const TypeDecl& node) override {
        print_indent();
        printf("TypeDecl: %s\n", node.name.c_str());
    }
};

#endif // TULPAR_AST_VISITOR_HPP
