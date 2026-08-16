#ifndef SEMA_H
#define SEMA_H

#include <string>
#include <vector>
#include <set>
#include <memory>
#include "AST.h"
#include "SymbolTable.h"

// 语义分析错误
struct SemaError
{
    int line;
    int column;
    std::string message;

    SemaError(int line, int column, std::string message)
        : line(line), column(column), message(std::move(message)) {}
};

// 语义分析警告
struct SemaWarning
{
    int line;
    int column;
    std::string message;

    SemaWarning(int line, int column, std::string message)
        : line(line), column(column), message(std::move(message)) {}
};

class Sema
{
private:
    SymbolTable symbols;
    std::vector<SemaError> errors;
    std::vector<SemaWarning> warnings;
    std::string currentReturnType;      // 当前函数返回类型（空=无返回）
    std::string currentFunctionName;    // 当前函数名
    std::string currentPackage;         // 当前分析的函数所属包（可见性检查用）
    std::set<std::string> importedModules;  // 已导入的标准库模块（如 "std.thread"）
    std::unordered_map<std::string, std::string> arrayElementTypes;    // 数组变量 → 元素类型
    std::unordered_map<std::string, std::string> pointerElementTypes;  // 指针变量/参数 → 指向类型

    // 结构体注册表：名字 → 成员（字段名 + 类型名）
    struct StructInfo
    {
        std::vector<std::pair<std::string, std::string>> fields;
        std::unordered_map<std::string, std::string> fieldElementTypes; // 数组成员 → 元素类型
    };
    std::unordered_map<std::string, StructInfo> structRegistry;
    std::set<std::string> functionLabels;   // 当前函数的 label 集合
    std::set<std::string> duplicateLabels;  // 重复 label 检测

public:
    bool analyze(std::unique_ptr<ProgramNode>& program);
    const std::vector<SemaError>& getErrors() const { return errors; }
    const std::vector<SemaWarning>& getWarnings() const { return warnings; }

private:
    void error(int line, int column, const std::string& message);
    void warn(int line, int column, const std::string& message);

    // 声明检查
    void visitProgram(ProgramNode* node);
    void visitDecl(ASTNode* node);
    void visitFunctionDecl(FunctionDeclNode* node);
    void visitStructDecl(StructDeclNode* node);
    void visitImplDecl(ImplDeclNode* node);

    // 语句检查
    void visitStmt(ASTNode* node);
    void visitBlock(BlockStmtNode* node);
    void visitVarDecl(VariableDeclNode* node);
    void visitIf(IfStmtNode* node);
    void visitWhile(WhileStmtNode* node);
    void visitFor(ForStmtNode* node);
    void visitGoto(GotoStmtNode* node);
    void visitLabel(LabelStmtNode* node);
    void visitSwitch(SwitchStmtNode* node);
    void visitReturn(ReturnStmtNode* node);
    void visitExprStmt(ExpressionStmtNode* node);

    // 表达式检查（返回推断出的类型名，空串表示无/void）
    std::string visitExpr(ASTNode* node);
    std::string visitBinary(BinaryOpNode* node);
    std::string visitUnary(UnaryOpNode* node);
    std::string visitComparison(ComparisonOpNode* node);
    std::string visitLogical(LogicalOpNode* node);
    std::string visitCall(FunctionCallNode* node);
    std::string visitThreadCall(FunctionCallNode* node);   // std.thread 内置 API 校验
    std::string visitAtomicCall(FunctionCallNode* node);   // std.atomic 内置 API 校验
    std::string visitLiteralInt(LiteralIntNode* node);
    std::string visitLiteralFloat(LiteralFloatNode* node);
    std::string visitLiteralString(LiteralStringNode* node);
    std::string visitLiteralBool(LiteralBoolNode* node);
    std::string visitVariableRef(VariableRefNode* node);
    std::string visitIndex(IndexNode* node);    // 数组下标 buf[i]
    std::string visitAssignment(AssignmentNode* node);
    void collectLabels(ASTNode* node);      // 预扫描函数体收集 label

    // 类型工具
    std::string typeNodeToName(TypeNode* type);
    bool isBuiltinType(const std::string& type) const;
    bool isNumericType(const std::string& type) const;
    bool isCompatible(const std::string& from, const std::string& to) const;
    bool isWidening(const std::string& from, const std::string& to) const;
};

#endif
