#ifndef TULPAR_AST_NODES_HPP
#define TULPAR_AST_NODES_HPP

#include <memory>
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <type_traits>
#include <utility>
#include "../lexer/lexer.hpp"

// Forward declarations
struct ASTNode;

// Source location for error messages
struct SourceLocation {
    int line;
    int column;
    
    SourceLocation(int l = 0, int c = 0) : line(l), column(c) {}
};

// Data type enumeration
enum DataType {
    TYPE_UNKNOWN = 0,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_CUSTOM,
    TYPE_ARRAY,
    TYPE_ARRAY_INT,
    TYPE_ARRAY_FLOAT,
    TYPE_ARRAY_STR,
    TYPE_ARRAY_BOOL,
    TYPE_ARRAY_JSON,
    TYPE_VOID
};

// ============================================================================
// Literal Nodes
// ============================================================================

struct IntLiteral {
    long long value;
    SourceLocation loc;
    
    IntLiteral(long long v, SourceLocation l) : value(v), loc(l) {}
};

struct FloatLiteral {
    double value;
    SourceLocation loc;
    
    FloatLiteral(double v, SourceLocation l) : value(v), loc(l) {}
};

struct StringLiteral {
    std::string value;
    SourceLocation loc;
    
    StringLiteral(const std::string& v, SourceLocation l) : value(v), loc(l) {}
};

struct BoolLiteral {
    bool value;
    SourceLocation loc;
    
    BoolLiteral(bool v, SourceLocation l) : value(v), loc(l) {}
};

struct Identifier {
    std::string name;
    SourceLocation loc;
    
    Identifier(const std::string& n, SourceLocation l) : name(n), loc(l) {}
};

// ============================================================================
// Expression Nodes
// ============================================================================

struct BinaryOp {
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    TulparTokenType op;
    SourceLocation loc;
    
    BinaryOp(std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r,
             TulparTokenType o, SourceLocation location)
        : left(std::move(l)), right(std::move(r)), op(o), loc(location) {}
};

struct UnaryOp {
    std::unique_ptr<ASTNode> operand;
    TulparTokenType op;
    SourceLocation loc;
    
    UnaryOp(std::unique_ptr<ASTNode> operand, TulparTokenType o, SourceLocation l)
        : operand(std::move(operand)), op(o), loc(l) {}
};

struct ArrayLiteral {
    std::vector<std::unique_ptr<ASTNode>> elements;
    SourceLocation loc;
    
    ArrayLiteral(std::vector<std::unique_ptr<ASTNode>> elems, SourceLocation l)
        : elements(std::move(elems)), loc(l) {}
};

struct ObjectLiteral {
    std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> fields;
    SourceLocation loc;
    
    ObjectLiteral(std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> f,
                  SourceLocation l)
        : fields(std::move(f)), loc(l) {}
};

struct ArrayAccess {
    std::unique_ptr<ASTNode> object;
    std::unique_ptr<ASTNode> index;
    SourceLocation loc;
    
    ArrayAccess(std::unique_ptr<ASTNode> obj, std::unique_ptr<ASTNode> idx,
                SourceLocation l)
        : object(std::move(obj)), index(std::move(idx)), loc(l) {}
};

struct FunctionCall {
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> arguments;
    std::optional<std::vector<std::string>> argument_names;
    std::unique_ptr<ASTNode> receiver; // For method calls
    SourceLocation loc;
    
    FunctionCall(const std::string& n,
                 std::vector<std::unique_ptr<ASTNode>> args,
                 SourceLocation l)
        : name(n), arguments(std::move(args)), receiver(nullptr), loc(l) {}
};

// ============================================================================
// Statement Nodes
// ============================================================================

struct Parameter {
    std::string name;
    DataType type;
    std::optional<std::string> custom_type;
    
    Parameter(const std::string& n, DataType t)
        : name(n), type(t) {}
};

struct VariableDecl {
    std::string name;
    DataType data_type;
    std::optional<std::string> custom_type;
    std::unique_ptr<ASTNode> initializer;
    bool is_moved;
    SourceLocation loc;
    
    VariableDecl(const std::string& n, DataType dt,
                 std::unique_ptr<ASTNode> init, SourceLocation l)
        : name(n), data_type(dt), initializer(std::move(init)),
          is_moved(false), loc(l) {}
};

struct Assignment {
    std::string name;
    std::unique_ptr<ASTNode> value;
    SourceLocation loc;
    
    Assignment(const std::string& n, std::unique_ptr<ASTNode> v, SourceLocation l)
        : name(n), value(std::move(v)), loc(l) {}
};

struct CompoundAssign {
    std::string name;
    TulparTokenType op;
    std::unique_ptr<ASTNode> value;
    SourceLocation loc;
    
    CompoundAssign(const std::string& n, TulparTokenType o,
                   std::unique_ptr<ASTNode> v, SourceLocation l)
        : name(n), op(o), value(std::move(v)), loc(l) {}
};

struct IncrementOp {
    std::string name;
    SourceLocation loc;
    
    IncrementOp(const std::string& n, SourceLocation l) : name(n), loc(l) {}
};

struct DecrementOp {
    std::string name;
    SourceLocation loc;
    
    DecrementOp(const std::string& n, SourceLocation l) : name(n), loc(l) {}
};

struct FunctionDecl {
    std::string name;
    std::vector<Parameter> parameters;
    DataType return_type;
    std::optional<std::string> return_custom_type;
    std::unique_ptr<ASTNode> body;
    std::optional<std::string> receiver_type; // For methods
    SourceLocation loc;
    
    FunctionDecl(const std::string& n, std::vector<Parameter> params,
                 DataType ret_type, std::unique_ptr<ASTNode> b, SourceLocation l)
        : name(n), parameters(std::move(params)), return_type(ret_type),
          body(std::move(b)), loc(l) {}
};

struct IfStatement {
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> then_branch;
    std::unique_ptr<ASTNode> else_branch; // nullptr if no else
    SourceLocation loc;
    
    IfStatement(std::unique_ptr<ASTNode> cond, std::unique_ptr<ASTNode> then_b,
                std::unique_ptr<ASTNode> else_b, SourceLocation l)
        : condition(std::move(cond)), then_branch(std::move(then_b)),
          else_branch(std::move(else_b)), loc(l) {}
};

struct WhileLoop {
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> body;
    SourceLocation loc;
    
    WhileLoop(std::unique_ptr<ASTNode> cond, std::unique_ptr<ASTNode> b,
              SourceLocation l)
        : condition(std::move(cond)), body(std::move(b)), loc(l) {}
};

struct ForLoop {
    std::unique_ptr<ASTNode> init;
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> increment;
    std::unique_ptr<ASTNode> body;
    SourceLocation loc;
    
    ForLoop(std::unique_ptr<ASTNode> i, std::unique_ptr<ASTNode> c,
            std::unique_ptr<ASTNode> inc, std::unique_ptr<ASTNode> b,
            SourceLocation l)
        : init(std::move(i)), condition(std::move(c)),
          increment(std::move(inc)), body(std::move(b)), loc(l) {}
};

struct ForInLoop {
    std::string variable;
    std::unique_ptr<ASTNode> iterable;
    std::unique_ptr<ASTNode> body;
    SourceLocation loc;
    
    ForInLoop(const std::string& var, std::unique_ptr<ASTNode> iter,
              std::unique_ptr<ASTNode> b, SourceLocation l)
        : variable(var), iterable(std::move(iter)), body(std::move(b)), loc(l) {}
};

struct ReturnStatement {
    std::unique_ptr<ASTNode> value; // nullptr for void return
    SourceLocation loc;
    
    ReturnStatement(std::unique_ptr<ASTNode> v, SourceLocation l)
        : value(std::move(v)), loc(l) {}
};

struct BreakStatement {
    SourceLocation loc;
    BreakStatement(SourceLocation l) : loc(l) {}
};

struct ContinueStatement {
    SourceLocation loc;
    ContinueStatement(SourceLocation l) : loc(l) {}
};

struct Block {
    std::vector<std::unique_ptr<ASTNode>> statements;
    SourceLocation loc;
    
    Block(std::vector<std::unique_ptr<ASTNode>> stmts, SourceLocation l)
        : statements(std::move(stmts)), loc(l) {}
};

struct Program {
    std::vector<std::unique_ptr<ASTNode>> statements;
    
    Program(std::vector<std::unique_ptr<ASTNode>> stmts)
        : statements(std::move(stmts)) {}
};

struct TryCatch {
    std::unique_ptr<ASTNode> try_block;
    std::string catch_var;
    std::unique_ptr<ASTNode> catch_block;
    std::unique_ptr<ASTNode> finally_block; // nullptr if no finally
    SourceLocation loc;
    
    TryCatch(std::unique_ptr<ASTNode> try_b, const std::string& var,
             std::unique_ptr<ASTNode> catch_b, std::unique_ptr<ASTNode> finally_b,
             SourceLocation l)
        : try_block(std::move(try_b)), catch_var(var),
          catch_block(std::move(catch_b)), finally_block(std::move(finally_b)),
          loc(l) {}
};

struct ThrowStatement {
    std::unique_ptr<ASTNode> expression;
    SourceLocation loc;
    
    ThrowStatement(std::unique_ptr<ASTNode> expr, SourceLocation l)
        : expression(std::move(expr)), loc(l) {}
};

struct ImportStatement {
    std::string path;
    SourceLocation loc;
    
    ImportStatement(const std::string& p, SourceLocation l) : path(p), loc(l) {}
};

struct TypeDecl {
    std::string name;
    std::vector<std::string> field_names;
    std::vector<DataType> field_types;
    std::vector<std::optional<std::string>> field_custom_types;
    std::vector<std::unique_ptr<ASTNode>> field_defaults;
    SourceLocation loc;
    
    TypeDecl(const std::string& n, SourceLocation l) : name(n), loc(l) {}
};

// ============================================================================
// Main AST Node Variant
// ============================================================================

struct ASTNode {
    using Variant = std::variant<
        IntLiteral,
        FloatLiteral,
        StringLiteral,
        BoolLiteral,
        Identifier,
        BinaryOp,
        UnaryOp,
        ArrayLiteral,
        ObjectLiteral,
        ArrayAccess,
        FunctionCall,
        VariableDecl,
        Assignment,
        CompoundAssign,
        IncrementOp,
        DecrementOp,
        FunctionDecl,
        IfStatement,
        WhileLoop,
        ForLoop,
        ForInLoop,
        ReturnStatement,
        BreakStatement,
        ContinueStatement,
        Block,
        Program,
        TryCatch,
        ThrowStatement,
        ImportStatement,
        TypeDecl>;

    Variant value;

    ASTNode() = default;
    ASTNode(const ASTNode &) = default;
    ASTNode(ASTNode &&) noexcept = default;
    ASTNode &operator=(const ASTNode &) = default;
    ASTNode &operator=(ASTNode &&) noexcept = default;

    template <typename T,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, ASTNode>>>
    ASTNode(T &&v) : value(std::forward<T>(v)) {}
};

#endif // TULPAR_AST_NODES_HPP
