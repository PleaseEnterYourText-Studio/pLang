#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <memory>
#include "token.h"
#include "AST.h"

// 解析错误（支持多错误收集）
struct ParserError
{
    int line;
    int column;
    std::string message;
};

class Parser
{
private:
    std::vector<Token> tokens;
    std::vector<ParserError> errors;
    size_t pos;
    int errorLine = 0;
    int errorColumn = 0;

public:
    explicit Parser(std::vector<Token> tokens);

    std::unique_ptr<ProgramNode> parse();
    const std::vector<ParserError>& getErrors() const { return errors; }

    int getErrorLine() const { return errorLine; }
    int getErrorColumn() const { return errorColumn; }

private:
    // 基础工具
    Token peek() const;
    Token peek(size_t offset) const;
    Token previous() const;
    Token advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool matchAny(const std::vector<TokenType>& types);
    Token expect(TokenType type, const std::string& message);
    Token expectPathSegment(const std::string& message);
    bool isAtEnd() const;

    // 类型解析
    std::unique_ptr<TypeNode> parseType();
    std::unique_ptr<TypeNode> parseTypeSuffix();
    std::unique_ptr<TypeNode> parsePrimitiveType();

    // 顶层声明
    std::unique_ptr<ASTNode> parseDeclaration();
    std::unique_ptr<ASTNode> parsePackage();
    std::unique_ptr<ASTNode> parseImport();
    std::unique_ptr<ASTNode> parseUsing();
    std::unique_ptr<ASTNode> parseFunctionDecl();
    std::unique_ptr<ASTNode> parseStructDecl(bool allowAnonymous = false);
    std::unique_ptr<ASTNode> parseImplDecl();

    // 语句
    std::unique_ptr<ASTNode> parseStatement();
    std::unique_ptr<ASTNode> parseBlock();
    std::unique_ptr<ASTNode> parseAsm();
    std::unique_ptr<ASTNode> parseVarDecl();
    std::unique_ptr<ASTNode> parseIf();
    std::unique_ptr<ASTNode> parseWhile();
    std::unique_ptr<ASTNode> parseFor();
    std::unique_ptr<ASTNode> parseDoWhile();
    std::unique_ptr<ASTNode> parseGoto();
    std::unique_ptr<ASTNode> parseLabel();
    std::unique_ptr<ASTNode> parseSwitch();
    std::unique_ptr<ASTNode> parseReturn();
    std::unique_ptr<ASTNode> parseExprStmt();

    // 表达式（13 级优先级）
    std::unique_ptr<ASTNode> parseExpression();
    std::unique_ptr<ASTNode> parseAssignment();
    std::unique_ptr<ASTNode> parseLogicalOr();
    std::unique_ptr<ASTNode> parseLogicalAnd();
    std::unique_ptr<ASTNode> parseBitwiseOr();
    std::unique_ptr<ASTNode> parseBitwiseXor();
    std::unique_ptr<ASTNode> parseBitwiseAnd();
    std::unique_ptr<ASTNode> parseEquality();
    std::unique_ptr<ASTNode> parseRelational();
    std::unique_ptr<ASTNode> parseShift();
    std::unique_ptr<ASTNode> parseAdditive();
    std::unique_ptr<ASTNode> parseMultiplicative();
    std::unique_ptr<ASTNode> parseUnary();
    std::unique_ptr<ASTNode> parsePostfix();
    std::unique_ptr<ASTNode> parsePrimary();

    // 辅助
    std::unique_ptr<ASTNode> parseInitList();
    std::vector<std::unique_ptr<ASTNode>> parseArguments();
    std::vector<std::string> parseTypeParams();
};

#endif
