#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include "../include/AST.h"

struct ASTNode
{
    ASTNodeType type;
    int line;
    int column;

    ASTNode(ASTNodeType type, int line = 0, int column = 0): type(type), line(line), column(column) {};
    virtual ~ASTNode() = default;
};

struct LiteralIntNode : ASTNode 
{
    //TODO
};

struct LiteralFloatNode : ASTNode 
{
    //TODO
};

struct LiteralStringNode : ASTNode 
{
    std::string value;

    LiteralStringNode(const std::string& value, int line = 0, int column = 0) 
    : ASTNode(ASTNodeType::LITERAL_STRING, line, column), value(value) {};
};

struct LiteralBoolNode : ASTNode 
{
    bool value;

    LiteralBoolNode(bool value, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::LITERAL_BOOL, line, column), value(value) {};
};

struct VariableRefNode : ASTNode 
{
    std::string name;

    VariableRefNode(const std::string& name, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::VARIABLE_REF, line, column), name(name) {};
};

struct BinaryOpNode : ASTNode 
{
    BinaryOpType op;
    std::unique_ptr<ASTNode> lift;
    std::unique_ptr<ASTNode> right;

    BinaryOpNode(BinaryOpType op, std::unique_ptr<ASTNode> lift, std::unique_ptr<ASTNode> right, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::BINARY_OP, line, column), op(op), lift(std::move(lift)), right(std::move(right)) {};
};

struct UnaryOpNode : ASTNode 
{
    UnaryOpType op;
    std::unique_ptr<ASTNode> operand;

    UnaryOpNode(UnaryOpType op, std::unique_ptr<ASTNode> operaend, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::UNARY_OP, line, column), op(op), operand(std::move(operand)) {};
};

struct ComparisonOpNode : ASTNode 
{
    ComparisonOpType op;
    std::unique_ptr<ASTNode> lift;
    std::unique_ptr<ASTNode> right;

    ComparisonOpNode(ComparisonOpType op, std::unique_ptr<ASTNode> lift, std::unique_ptr<ASTNode> right, int line = 0, int column = 0)
    : ASTNode(ASTNodeType::COMPARISON_OP, line, column), op(op), lift(std::move(lift)), right(std::move(right)) {};
};

struct LogicalOpNode : ASTNode 
{
    LogicalOpType op;
    std::unique_ptr<ASTNode> lift;
    std::unique_ptr<ASTNode> right;

    LogicalOpNode(LogicalOpType op, std::unique_ptr<ASTNode> lift, std::unique_ptr<ASTNode> right, int line = 0, int column = 0)
    :ASTNode(ASTNodeType::LOGICAL_OP, line, column), op(op), lift(std::move(lift)), right(std::move(right)) {};
};

struct ConditionalExprNode : ASTNode
{
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> thenExpr;
    std::unique_ptr<ASTNode> elseExpr;

    ConditionalExprNode(std::unique_ptr<ASTNode> condition, std::unique_ptr<ASTNode> thenExpr, std::unique_ptr<ASTNode> elseExpr, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::CONDITIONAL_EXPR, line, column), condition(std::move(condition)), thenExpr(std::move(thenExpr)), elseExpr(std::move(elseExpr)) {}
};

struct FunctionCallNode : ASTNode
{
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> arguments;

    FunctionCallNode(const std::string& name, std::vector<std::unique_ptr<ASTNode>> arguments, int line = 0, int column = 0)
        : ASTNode(ASTNodeType::FUNCTION_CALL, line, column), name(name), arguments(std::move(arguments)) {}
};
