#ifndef AST_H
#define AST_H

#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

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
    DEC // --a
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

// 整形节点(TODO：后续细化)
struct LiteralIntNode : ASTNode 
{
    //TODO
};

// 浮点数节点（TODO：后续细化）
struct LiteralFloatNode : ASTNode 
{
    //TODO
};

//字符串节点
struct LiteralStringNode : ASTNode 
{
    std::string value;

    LiteralStringNode(const std::string& value, int line = 0, int column = 0) 
    : ASTNode(ASTNodeType::LITERAL_STRING, line, column), value(value) {};
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

#endif