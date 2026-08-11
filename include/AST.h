#ifndef AST_H
#define AST_H

#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include "token.h"

enum class ASTNodeType
{
    // 语句
    PROGRAM,
    EXPRESSION_STMT,
    VARIABLE_DECL,
    ASSIGNMENT_STMT,
    IF_STMT,
    WHILE_STMT,
    FOR_STMT,
    RETURN_STMT,
    BLOCK_STMT,
    FUNCTION_DECL,
    FUNCTION_CALL,

    // 声明
    PACKAGE_STMT,
    IMPORT_STMT,
    USING_DECL,
    STRUCT_DECL,
    IMPL_DECL,

    // 表达式
    LITERAL_INT,
    LITERAL_FLOAT,
    LITERAL_STRING,
    LITERAL_BOOL,
    VARIABLE_REF,
    BINARY_OP,
    UNARY_OP,
    COMPARISON_OP,
    LOGICAL_OP,
    CONDITIONAL_EXPR,
    CAST,
    CAST_EXPR,
    THIS_REF,
    TYPE_PARAM,
    TEMPLATE_DECL,
    DO_WHILE_STMT,
    MEMBER_ACCESS,
    ADDRESS_OF,
    DEREF,
    STRUCT_INIT,
    SIZEOF_EXPR,

    // 类型
    TYPE_PRIMITIVE,
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_REFERENCE,
    TYPE_TYPE,

    // 类型
    TYPE_I8,
    TYPE_I16,
    TYPE_I32,
    TYPE_I64,
    TYPE_I128,
    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    TYPE_U64,
    TYPE_U128,
    TYPE_F32,
    TYPE_F64,
    TYPE_STRING,
    TYPE_CHAR,
    TYPE_BOOL,
    TYPE_VOID
};

enum class BinaryOpType
{
    ADD, // +
    SUB, // -
    MUL, // *
    DIV, // /
    MOD // %
};

enum class UnaryOpType
{
    NEG, // -a
    NOT, // !a
    INC, // ++a
    DEC, // --a
    DEREF // *a
};

enum class ComparisonOpType
{
    EQ,  // a == b
    NE,  // a != b
    LT,  // a < b
    LE,  // a <= b
    GT,  // a > b
    GE   // a >= b
};

enum class LogicalOpType
{
    AND,  // a && b
    OR    // a || b
};

// 基类节点
struct ASTNode
{
    ASTNodeType type;
    int line;
    int column;

    ASTNode(ASTNodeType type, int line = 0, int column = 0): type(type), line(line), column(column) {};
    virtual ~ASTNode() = default;
};

// 整形节点
struct LiteralIntNode : ASTNode 
{
    long long value;
    std::string suffix;     // ""=int, "ll"=i64

    LiteralIntNode(long long value, int line = 0, int column = 0, const std::string& suffix = "")
    : ASTNode(ASTNodeType::LITERAL_INT, line, column), value(value), suffix(suffix) {};
};

// 浮点数节点
struct LiteralFloatNode : ASTNode 
{
    double value;
    std::string suffix;     // ""=f64, "f"=f32

    LiteralFloatNode(double value, int line = 0, int column = 0, const std::string& suffix = "")
    : ASTNode(ASTNodeType::LITERAL_FLOAT, line, column), value(value), suffix(suffix) {};
};

//字符串节点
struct LiteralStringNode : ASTNode 
{
    std::string value;
    bool isChar;

    LiteralStringNode(const std::string& value, int line = 0, int column = 0, bool isChar = false) 
    : ASTNode(ASTNodeType::LITERAL_STRING, line, column), value(value), isChar(isChar) {};
};

//布尔节点
struct LiteralBoolNode : ASTNode 
{
    bool value;

    LiteralBoolNode(bool value, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::LITERAL_BOOL, line, column), value(value) {};
};

//引用节点
struct VariableRefNode : ASTNode 
{
    std::string name;

    VariableRefNode(const std::string& name, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::VARIABLE_REF, line, column), name(name) {};
};

//二元运算节点
struct BinaryOpNode : ASTNode 
{
    BinaryOpType op;
    std::unique_ptr<ASTNode> lift;
    std::unique_ptr<ASTNode> right;

    BinaryOpNode(BinaryOpType op, std::unique_ptr<ASTNode> lift, std::unique_ptr<ASTNode> right, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::BINARY_OP, line, column), op(op), lift(std::move(lift)), right(std::move(right)) {};
};

//一元运算节点
struct UnaryOpNode : ASTNode 
{
    UnaryOpType op;
    std::unique_ptr<ASTNode> operand;

    UnaryOpNode(UnaryOpType op, std::unique_ptr<ASTNode> operand, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::UNARY_OP, line, column), op(op), operand(std::move(operand)) {};
};

// 取地址节点
struct AddressOfNode : ASTNode
{
    std::unique_ptr<ASTNode> operand;

    explicit AddressOfNode(std::unique_ptr<ASTNode> operand, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::UNARY_OP, line, column), operand(std::move(operand)) {}
};

// 类型转换节点
struct CastNode : ASTNode
{
    std::string targetType;
    std::unique_ptr<ASTNode> value;

    CastNode(const std::string& targetType, std::unique_ptr<ASTNode> value, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::CAST, line, column), targetType(targetType), value(std::move(value)) {}
};

//比较运算节点
struct ComparisonOpNode : ASTNode 
{
    ComparisonOpType op;
    std::unique_ptr<ASTNode> lift;
    std::unique_ptr<ASTNode> right;

    ComparisonOpNode(ComparisonOpType op, std::unique_ptr<ASTNode> lift, std::unique_ptr<ASTNode> right, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::COMPARISON_OP, line, column), op(op), lift(std::move(lift)), right(std::move(right)) {};
};

//逻辑运算节点
struct LogicalOpNode : ASTNode 
{
    LogicalOpType op;
    std::unique_ptr<ASTNode> lift;
    std::unique_ptr<ASTNode> right;

    LogicalOpNode(LogicalOpType op, std::unique_ptr<ASTNode> lift, std::unique_ptr<ASTNode> right, int line = 0, int column = 0)
    :ASTNode(ASTNodeType::LOGICAL_OP, line, column), op(op), lift(std::move(lift)), right(std::move(right)) {};
};

//条件表达式节点
struct ConditionalExprNode : ASTNode
{
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> thenExpr;
    std::unique_ptr<ASTNode> elseExpr;

    ConditionalExprNode(std::unique_ptr<ASTNode> condition, std::unique_ptr<ASTNode> thenExpr, std::unique_ptr<ASTNode> elseExpr, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::CONDITIONAL_EXPR, line, column), condition(std::move(condition)), thenExpr(std::move(thenExpr)), elseExpr(std::move(elseExpr)) {}
};

// 函数节点
struct FunctionCallNode : ASTNode
{
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> arguments;

    FunctionCallNode(const std::string& name, std::vector<std::unique_ptr<ASTNode>> arguments, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::FUNCTION_CALL, line, column), name(name), arguments(std::move(arguments)) {}
};

// 类型节点
struct TypeNode : ASTNode
{
    ASTNodeType baseType;
    std::string name;
    int arraySize;
    std::unique_ptr<TypeNode> inner;
    bool isConst;

    TypeNode(ASTNodeType type, const std::string& name = "", int line = 0, int column = 0, int arraySize = 0, std::unique_ptr<TypeNode> inner = nullptr, bool isConst = false)
        : ASTNode(type, line, column), baseType(type), name(name), arraySize(arraySize), inner(std::move(inner)), isConst(isConst) {}
};

// 程序根节点
struct ProgramNode : ASTNode
{
    std::string packageName;
    std::vector<std::unique_ptr<ASTNode>> imports;
    std::vector<std::unique_ptr<ASTNode>> decls;

    ProgramNode(int line = 0, int column = 0)
        : ASTNode(ASTNodeType::PROGRAM, line, column) {}
};

// package 语句
struct PackageStmtNode : ASTNode
{
    std::string name;

    PackageStmtNode(const std::string& name, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::PACKAGE_STMT, line, column), name(name) {}
};

// import 语句
struct ImportStmtNode : ASTNode
{
    std::string path;

    ImportStmtNode(const std::string& path, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::IMPORT_STMT, line, column), path(path) {}
};

// using 别名声明
struct UsingDeclNode : ASTNode
{
    std::string name;
    std::unique_ptr<ASTNode> aliased;

    UsingDeclNode(const std::string& name, std::unique_ptr<ASTNode> aliased, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::USING_DECL, line, column), name(name), aliased(std::move(aliased)) {}
};

// 变量声明
struct VariableDeclNode : ASTNode
{
    bool isVar;                 // true=var, false=val
    bool isMoved;               // move 修饰
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<ASTNode> initializer;

    VariableDeclNode(bool isVar, bool isMoved, const std::string& name, std::unique_ptr<TypeNode> type,
                     std::unique_ptr<ASTNode> initializer, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::VARIABLE_DECL, line, column), isVar(isVar), isMoved(isMoved), name(name),
          type(std::move(type)), initializer(std::move(initializer)) {}
};

// 赋值语句
struct AssignmentNode : ASTNode
{
    std::unique_ptr<ASTNode> target;
    std::unique_ptr<ASTNode> value;
    TokenType op;               // = += -= 等

    AssignmentNode(std::unique_ptr<ASTNode> target, std::unique_ptr<ASTNode> value, TokenType op, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::ASSIGNMENT_STMT, line, column), target(std::move(target)),
          value(std::move(value)), op(op) {}
};

// return 语句
struct ReturnStmtNode : ASTNode
{
    std::unique_ptr<ASTNode> value;

    ReturnStmtNode(std::unique_ptr<ASTNode> value, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::RETURN_STMT, line, column), value(std::move(value)) {}
};

// 表达式语句
struct ExpressionStmtNode : ASTNode
{
    std::unique_ptr<ASTNode> expr;

    ExpressionStmtNode(std::unique_ptr<ASTNode> expr, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::EXPRESSION_STMT, line, column), expr(std::move(expr)) {}
};

// 块语句
struct BlockStmtNode : ASTNode
{
    std::vector<std::unique_ptr<ASTNode>> statements;

    BlockStmtNode(int line = 0, int column = 0)
        : ASTNode(ASTNodeType::BLOCK_STMT, line, column) {}
};

// if 语句
struct IfStmtNode : ASTNode
{
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> thenBranch;
    std::unique_ptr<ASTNode> elseBranch;

    IfStmtNode(std::unique_ptr<ASTNode> condition, std::unique_ptr<ASTNode> thenBranch,
               std::unique_ptr<ASTNode> elseBranch, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::IF_STMT, line, column), condition(std::move(condition)),
          thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}
};

// while 语句
struct WhileStmtNode : ASTNode
{
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> body;

    WhileStmtNode(std::unique_ptr<ASTNode> condition, std::unique_ptr<ASTNode> body, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::WHILE_STMT, line, column), condition(std::move(condition)), body(std::move(body)) {}
};

// for 语句
struct ForStmtNode : ASTNode
{
    std::unique_ptr<ASTNode> init;
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> update;
    std::unique_ptr<ASTNode> body;

    ForStmtNode(std::unique_ptr<ASTNode> init, std::unique_ptr<ASTNode> condition,
                std::unique_ptr<ASTNode> update, std::unique_ptr<ASTNode> body, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::FOR_STMT, line, column), init(std::move(init)),
          condition(std::move(condition)), update(std::move(update)), body(std::move(body)) {}
};

// 参数节点
struct ParameterNode : ASTNode
{
    bool isVar;                 // val/var
    std::string name;
    std::unique_ptr<TypeNode> type;

    ParameterNode(bool isVar, const std::string& name, std::unique_ptr<TypeNode> type, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::VARIABLE_DECL, line, column), isVar(isVar), name(name), type(std::move(type)) {}
};

// 函数声明
struct FunctionDeclNode : ASTNode
{
    std::string name;
    std::vector<std::unique_ptr<ParameterNode>> params;
    std::unique_ptr<TypeNode> returnType;
    std::unique_ptr<BlockStmtNode> body;
    bool hasBody;

    FunctionDeclNode(const std::string& name, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::FUNCTION_DECL, line, column), name(name), hasBody(false) {}
};

// struct 声明
struct StructDeclNode : ASTNode
{
    std::string name;
    bool isAbstract;
    std::vector<std::string> bases;
    std::vector<std::unique_ptr<ASTNode>> members;

    StructDeclNode(const std::string& name, bool isAbstract, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::STRUCT_DECL, line, column), name(name), isAbstract(isAbstract) {}
};

// impl 实现
struct ImplDeclNode : ASTNode
{
    std::string target;
    std::vector<std::unique_ptr<ASTNode>> members;

    ImplDeclNode(const std::string& target, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::IMPL_DECL, line, column), target(target) {}
};

// ==================== 以下节点结构来自 main 分支 (81fd524) ====================
// 作为后续功能实现的设计参考，构造函数已补全内联实现

// ===== do-while 语句 =====
struct DoWhileStmtNode : ASTNode
{
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> body;

    DoWhileStmtNode(std::unique_ptr<ASTNode> condition, std::unique_ptr<ASTNode> body, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::WHILE_STMT, line, column), condition(std::move(condition)), body(std::move(body)) {}
};

// ===== 成员访问 =====
struct MemberAccessNode : ASTNode
{
    std::unique_ptr<ASTNode> object;
    std::string member;
    bool isMethodCall;

    MemberAccessNode(std::unique_ptr<ASTNode> object, const std::string& member, bool isMethodCall = false, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::VARIABLE_REF, line, column), object(std::move(object)), member(member), isMethodCall(isMethodCall) {}
};

// ===== 解引用 =====
struct DerefNode : ASTNode
{
    std::unique_ptr<ASTNode> operand;

    DerefNode(std::unique_ptr<ASTNode> operand, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::UNARY_OP, line, column), operand(std::move(operand)) {}
};

// ===== 引用类型 =====
struct ReferenceTypeNode : TypeNode
{
    bool isMutable;

    ReferenceTypeNode(std::unique_ptr<TypeNode> inner, bool isMutable, int line = 0, int column = 0)
        : TypeNode(ASTNodeType::TYPE_REFERENCE, "", line, column, 0, std::move(inner)), isMutable(isMutable) {}
};

// ===== 模板参数 =====
struct TemplateParamNode : ASTNode
{
    std::string name;
    bool isTypeParam;

    TemplateParamNode(const std::string& name, bool isTypeParam, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::TYPE_PARAM, line, column), name(name), isTypeParam(isTypeParam) {}
};

// ===== 模板声明 =====
struct TemplateDeclNode : ASTNode
{
    std::vector<std::unique_ptr<TemplateParamNode>> params;
    std::unique_ptr<ASTNode> body;

    TemplateDeclNode(std::vector<std::unique_ptr<TemplateParamNode>> params, std::unique_ptr<ASTNode> body, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::TEMPLATE_DECL, line, column), params(std::move(params)), body(std::move(body)) {}
};

// ===== this 引用 =====
struct ThisRefNode : ASTNode
{
    ThisRefNode(int line = 0, int column = 0)
        : ASTNode(ASTNodeType::THIS_REF, line, column) {}
};

// ===== 类型转换 =====
struct CastExprNode : ASTNode
{
    std::unique_ptr<ASTNode> expr;
    std::unique_ptr<TypeNode> targetType;

    CastExprNode(std::unique_ptr<ASTNode> expr, std::unique_ptr<TypeNode> targetType, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::CAST_EXPR, line, column), expr(std::move(expr)), targetType(std::move(targetType)) {}
};

// ===== sizeof 表达式 =====
struct SizeofExprNode : ASTNode
{
    std::unique_ptr<TypeNode> targetType;

    explicit SizeofExprNode(std::unique_ptr<TypeNode> targetType, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::SIZEOF_EXPR, line, column), targetType(std::move(targetType)) {}
};

#endif