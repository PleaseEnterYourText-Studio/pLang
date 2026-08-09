#ifndef AST_H
#define AST_H

#include <iostream>

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
struct ASTNode {};

// 整形节点(TODO：后续细化)
struct LiteralIntNode : ASTNode {};

// 浮点数节点（TODO：后续细化）
struct LiteralFloatNode : ASTNode {};

//字符串节点
struct LiteralStringNode : ASTNode {};

//布尔节点
struct LiteralBoolNode : ASTNode {};

//引用节点
struct VariableRefNode : ASTNode {};

//二元运算节点
struct BinaryOpNode : ASTNode {};

//一元运算节点
struct UnaryOpNode : ASTNode {};

//比较运算节点
struct ComparisonOpNode : ASTNode {};

//逻辑运算节点
struct LogicalOpNode : ASTNode {};

//条件表达式节点
struct ConditionalExprNode : ASTNode {};

// 函数节点
struct FunctionCallNode : ASTNode {};

#endif