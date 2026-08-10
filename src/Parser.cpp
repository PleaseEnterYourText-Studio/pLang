#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "../include/token.h"
#include "../include/AST.h"
#include "../include/Parser.h"

Parser::Parser(std::vector<Token> tokens) : tokens(std::move(tokens)), pos(0) {}

std::unique_ptr<ProgramNode> Parser::parse()
{
    auto program = std::make_unique<ProgramNode>();

    // package 声明（可选但应在前）
    if (match(TokenType::PACKAGE))
    {
        auto pkg = parsePackage();
        auto pkgNode = dynamic_cast<PackageStmtNode*>(pkg.get());
        if (pkgNode) program->packageName = pkgNode->name;
    }

    while (!isAtEnd())
    {
        if (match(TokenType::IMPORT))
        {
            program->imports.push_back(parseImport());
        }
        else
        {
            auto decl = parseDeclaration();
            if (decl) program->decls.push_back(std::move(decl));
        }
    }
    return program;
}

bool Parser::isAtEnd() const
{
    return peek().type == TokenType::EOF_TOKEN;
}

Token Parser::peek() const
{
    return tokens[pos];
}

Token Parser::previous() const
{
    return tokens[pos - 1];
}

Token Parser::advance()
{
    if (!isAtEnd()) pos++;
    return previous();
}

bool Parser::check(TokenType type) const
{
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(TokenType type)
{
    if (check(type))
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::matchAny(const std::vector<TokenType>& types)
{
    for (auto t : types)
    {
        if (check(t))
        {
            advance();
            return true;
        }
    }
    return false;
}

Token Parser::expect(TokenType type, const std::string& message)
{
    if (check(type)) return advance();
    std::cerr << "syntax error " << peek().line << ":" << peek().column << " - " << message
              << " (expected " << static_cast<int>(type) << ", got " << peek().text << ")\n";
    throw std::runtime_error("parse error");
}

std::unique_ptr<TypeNode> Parser::parseType()
{
    // 指针类型: var/val -> var/val [: T]
    if (check(TokenType::VAR) || check(TokenType::VAL))
    {
        advance();
        if (match(TokenType::ARROW))
        {
            bool innerConst = false;
            if (match(TokenType::VAR)) { /* rw */ }
            else if (match(TokenType::VAL)) { innerConst = true; }
            auto base = parsePrimitiveType();
            std::unique_ptr<TypeNode> inner = std::move(base);
            if (match(TokenType::COLON))
            {
                inner = parseType();
            }
            auto ptr = std::make_unique<TypeNode>(ASTNodeType::TYPE_POINTER, "", peek().line, peek().column,
                                                  0, std::move(inner), innerConst);
            return ptr;
        }
        // var/val 开头但不是指针，回退
        pos--;
    }

    // 数组类型: T[expr]
    auto t = parsePrimitiveType();

    // 后缀数组 T[n]
    while (match(TokenType::LBRACKET))
    {
        int size = 0;
        if (check(TokenType::NUMBER))
        {
            size = std::stoi(advance().text);
        }
        expect(TokenType::RBRACKET, "expected ] to close array length");
        t = std::make_unique<TypeNode>(ASTNodeType::TYPE_ARRAY, "", peek().line, peek().column,
                                       size, std::move(t));
    }
    return t;
}

std::unique_ptr<TypeNode> Parser::parsePrimitiveType()
{
    Token tok = peek();
    std::string name = tok.text;

    ASTNodeType t;
    switch (tok.type)
    {
        case TokenType::I8: t = ASTNodeType::TYPE_I8; break;
        case TokenType::I16: t = ASTNodeType::TYPE_I16; break;
        case TokenType::I32: t = ASTNodeType::TYPE_I32; break;
        case TokenType::I64: t = ASTNodeType::TYPE_I64; break;
        case TokenType::INT: t = ASTNodeType::TYPE_I32; break;
        case TokenType::U8: t = ASTNodeType::TYPE_U8; break;
        case TokenType::U16: t = ASTNodeType::TYPE_U16; break;
        case TokenType::U32: t = ASTNodeType::TYPE_U32; break;
        case TokenType::U64: t = ASTNodeType::TYPE_U64; break;
        case TokenType::UINT: t = ASTNodeType::TYPE_U32; break;
        case TokenType::F32: t = ASTNodeType::TYPE_F32; break;
        case TokenType::F64: t = ASTNodeType::TYPE_F64; break;
        case TokenType::CHAR: t = ASTNodeType::TYPE_CHAR; break;
        case TokenType::STRING_TYPE: t = ASTNodeType::TYPE_STRING; break;
        case TokenType::BOOL: t = ASTNodeType::TYPE_BOOL; break;
        case TokenType::THIS_TYPE: t = ASTNodeType::TYPE_TYPE; break;
        case TokenType::IDENT: t = ASTNodeType::TYPE_PRIMITIVE; break;
        default:
            std::cerr << "syntax error " << tok.line << ":" << tok.column << " - expected type, got " << tok.text << "\n";
            throw std::runtime_error("parse error");
    }
    advance();
    return std::make_unique<TypeNode>(t, name, tok.line, tok.column);
}

std::unique_ptr<ASTNode> Parser::parseDeclaration()
{
    if (match(TokenType::USING)) return parseUsing();
    if (check(TokenType::FUNC)) return parseFunctionDecl();
    if (check(TokenType::STRUCT) || check(TokenType::ABSTRACT)) return parseStructDecl();
    if (check(TokenType::IMPL)) return parseImplDecl();

    std::cerr << "syntax error " << peek().line << ":" << peek().column
              << " - expected top-level declaration (using/func/struct/impl), got " << peek().text << "\n";
    throw std::runtime_error("parse error");
}

std::unique_ptr<ASTNode> Parser::parsePackage()
{
    Token name = expect(TokenType::IDENT, "expected package name");
    expect(TokenType::SEMICOLON, "expected ;");
    return std::make_unique<PackageStmtNode>(name.text, name.line, name.column);
}

std::unique_ptr<ASTNode> Parser::parseImport()
{
    std::string path;
    Token first = expect(TokenType::IDENT, "expected import path");
    path = first.text;
    while (match(TokenType::DOT))
    {
        Token part = expect(TokenType::IDENT, "expected path segment");
        path += "." + part.text;
    }
    expect(TokenType::SEMICOLON, "expected ;");
    return std::make_unique<ImportStmtNode>(path, first.line, first.column);
}

std::unique_ptr<ASTNode> Parser::parseUsing()
{
    Token name = expect(TokenType::IDENT, "expected alias name");
    expect(TokenType::ASSIGN, "expected =");
    std::unique_ptr<ASTNode> aliased;

    if (check(TokenType::STRUCT) || check(TokenType::ABSTRACT))
    {
        aliased = parseStructDecl();
    }
    else
    {
        // using x = std.vector<i32>;
        std::string path = expect(TokenType::IDENT, "expected type path").text;
        while (match(TokenType::DOT))
        {
            path += "." + expect(TokenType::IDENT, "expected path segment").text;
        }
        std::unique_ptr<TypeNode> ty;
        if (match(TokenType::LT))
        {
            // 泛型参数：解析第一个类型参数（简化）
            auto firstArg = parseType();
            while (match(TokenType::COMMA)) { parseType(); }
            expect(TokenType::GT, "expected > to close generics");
            ty = std::make_unique<TypeNode>(ASTNodeType::TYPE_PRIMITIVE, path, name.line, name.column);
        }
        else
        {
            ty = std::make_unique<TypeNode>(ASTNodeType::TYPE_PRIMITIVE, path, name.line, name.column);
        }
        expect(TokenType::SEMICOLON, "expected ;");
        aliased = std::move(ty);
    }
    return std::make_unique<UsingDeclNode>(name.text, std::move(aliased), name.line, name.column);
}

std::unique_ptr<ASTNode> Parser::parseFunctionDecl()
{
    if (!match(TokenType::FUNC))
    {
        expect(TokenType::FUNC, "expected func");
    }
    Token name = expect(TokenType::IDENT, "expected function name");

    // 模板参数 <T: type> 或 <a: int>
    std::vector<std::string> typeParams;
    if (match(TokenType::LT))
    {
        typeParams = parseTypeParams();
    }

    auto fn = std::make_unique<FunctionDeclNode>(name.text, name.line, name.column);

    expect(TokenType::LPAREN, "expected (");
    if (!check(TokenType::RPAREN))
    {
        do
        {
            bool isVar = true;
            if (match(TokenType::VAL)) isVar = false;
            else if (match(TokenType::VAR)) isVar = true;
            else throw std::runtime_error("expected parameter modifier val/var");

            auto type = parseType();
            Token pname = expect(TokenType::IDENT, "expected parameter name");
            fn->params.push_back(std::make_unique<ParameterNode>(isVar, pname.text, std::move(type),
                                                                 pname.line, pname.column));
        } while (match(TokenType::COMMA));
    }
    expect(TokenType::RPAREN, "expected )");

    // 返回类型
    if (match(TokenType::ARROW))
    {
        fn->returnType = parseType();
    }

    if (match(TokenType::SEMICOLON))
    {
        fn->hasBody = false; // 仅声明，由 impl 实现
        return fn;
    }

    fn->hasBody = true;
    fn->body = std::make_unique<BlockStmtNode>(peek().line, peek().column);
    auto block = parseBlock();
    fn->body = std::unique_ptr<BlockStmtNode>(dynamic_cast<BlockStmtNode*>(block.release()));
    return fn;
}

std::unique_ptr<ASTNode> Parser::parseStructDecl()
{
    bool isAbstract = match(TokenType::ABSTRACT);
    expect(TokenType::STRUCT, "expected struct");

    Token name = expect(TokenType::IDENT, "expected struct name");
    auto st = std::make_unique<StructDeclNode>(name.text, isAbstract, name.line, name.column);

    // 泛型 <T: type>
    if (match(TokenType::LT))
    {
        parseTypeParams();
    }

    // 继承 : pub A, B
    if (match(TokenType::COLON))
    {
        do
        {
            match(TokenType::PUB);
            st->bases.push_back(expect(TokenType::IDENT, "expected base class name").text);
        } while (match(TokenType::COMMA));
    }

    expect(TokenType::LBRACE, "expected {");
    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        st->members.push_back(parseStatement());
    }
    expect(TokenType::RBRACE, "expected }");

    return st;
}

std::unique_ptr<ASTNode> Parser::parseImplDecl()
{
    expect(TokenType::IMPL, "expected impl");
    std::string target = expect(TokenType::IDENT, "expected target name").text;
    while (match(TokenType::DOT))
    {
        target += "." + expect(TokenType::IDENT, "expected member name").text;
    }

    auto impl = std::make_unique<ImplDeclNode>(target, peek().line, peek().column);
    expect(TokenType::LBRACE, "expected {");
    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        impl->members.push_back(parseStatement());
    }
    expect(TokenType::RBRACE, "expected }");
    return impl;
}

std::unique_ptr<ASTNode> Parser::parseStatement()
{
    if (check(TokenType::VAR) || check(TokenType::VAL)) return parseVarDecl();
    if (check(TokenType::IF)) return parseIf();
    if (check(TokenType::WHILE)) return parseWhile();
    if (check(TokenType::FOR)) return parseFor();
    if (check(TokenType::DO)) return parseDoWhile();
    if (check(TokenType::RETURN)) return parseReturn();
    if (check(TokenType::LBRACE)) return parseBlock();
    if (check(TokenType::SEMICOLON)) { advance(); return nullptr; }
    return parseExprStmt();
}

std::unique_ptr<ASTNode> Parser::parseBlock()
{
    Token start = expect(TokenType::LBRACE, "expected {");
    auto block = std::make_unique<BlockStmtNode>(start.line, start.column);
    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        auto stmt = parseStatement();
        if (stmt) block->statements.push_back(std::move(stmt));
    }
    expect(TokenType::RBRACE, "expected }");
    return block;
}

std::unique_ptr<ASTNode> Parser::parseVarDecl()
{
    bool isVar;
    if (match(TokenType::VAL)) isVar = false;
    else if (match(TokenType::VAR)) isVar = true;
    else throw std::runtime_error("expected var/val");

    bool isMoved = false;
    std::unique_ptr<TypeNode> type;
    std::string name;
    std::unique_ptr<ASTNode> init;

    // 指针类型: var -> var: T p
    if (check(TokenType::ARROW))
    {
        // 形如 var -> var: T name 的指针声明
        // 但 parseVarDecl 已在 parseStatement 中被检查到 VAR/VAL 开头的声明
        // 这里处理 var/val -> ... 形式
        type = parseType(); // parseType 内部处理 var/val -> ...
        name = expect(TokenType::IDENT, "expected variable name").text;
    }
    else if (check(TokenType::COLON))
    {
        advance();
        type = parseType();
        name = expect(TokenType::IDENT, "expected variable name").text;
    }
    else
    {
        // 类型推导: var a = 1;
        name = expect(TokenType::IDENT, "expected variable name").text;
    }

    if (match(TokenType::ASSIGN))
    {
        if (match(TokenType::MOVE))
        {
            isMoved = true;
        }
        init = parseExpression();
    }

    expect(TokenType::SEMICOLON, "expected ;");
    return std::make_unique<VariableDeclNode>(isVar, isMoved, name, std::move(type), std::move(init),
                                              previous().line, previous().column);
}

std::unique_ptr<ASTNode> Parser::parseIf()
{
    Token start = expect(TokenType::IF, "expected if");
    expect(TokenType::LPAREN, "expected (");
    auto cond = parseExpression();
    expect(TokenType::RPAREN, "expected )");
    auto then = parseStatement();
    std::unique_ptr<ASTNode> otherwise = nullptr;
    if (match(TokenType::ELSE))
    {
        otherwise = parseStatement();
    }
    return std::make_unique<IfStmtNode>(std::move(cond), std::move(then), std::move(otherwise),
                                        start.line, start.column);
}

std::unique_ptr<ASTNode> Parser::parseWhile()
{
    Token start = expect(TokenType::WHILE, "expected while");
    expect(TokenType::LPAREN, "expected (");
    auto cond = parseExpression();
    expect(TokenType::RPAREN, "expected )");
    auto body = parseStatement();
    return std::make_unique<WhileStmtNode>(std::move(cond), std::move(body), start.line, start.column);
}

std::unique_ptr<ASTNode> Parser::parseFor()
{
    Token start = expect(TokenType::FOR, "expected for");
    expect(TokenType::LPAREN, "expected (");
    std::unique_ptr<ASTNode> init = nullptr;
    if (!check(TokenType::SEMICOLON))
    {
        if (check(TokenType::VAR) || check(TokenType::VAL))
        {
            init = parseVarDecl(); // 消耗到 ;
        }
        else
        {
            init = parseExprStmt();
        }
    }
    else
    {
        advance();
    }

    std::unique_ptr<ASTNode> cond = nullptr;
    if (!check(TokenType::SEMICOLON))
    {
        cond = parseExpression();
    }
    expect(TokenType::SEMICOLON, "expected ;");

    std::unique_ptr<ASTNode> update = nullptr;
    if (!check(TokenType::RPAREN))
    {
        update = parseExpression();
    }
    expect(TokenType::RPAREN, "expected )");

    auto body = parseStatement();
    return std::make_unique<ForStmtNode>(std::move(init), std::move(cond), std::move(update),
                                         std::move(body), start.line, start.column);
}

std::unique_ptr<ASTNode> Parser::parseDoWhile()
{
    Token start = expect(TokenType::DO, "expected do");
    auto body = parseStatement();
    expect(TokenType::WHILE, "expected while");
    expect(TokenType::LPAREN, "expected (");
    auto cond = parseExpression();
    expect(TokenType::RPAREN, "expected )");
    expect(TokenType::SEMICOLON, "expected ;");
    return std::make_unique<WhileStmtNode>(std::move(cond), std::move(body), start.line, start.column);
}

std::unique_ptr<ASTNode> Parser::parseReturn()
{
    Token start = expect(TokenType::RETURN, "expected return");
    std::unique_ptr<ASTNode> value = nullptr;
    if (!check(TokenType::SEMICOLON))
    {
        value = parseExpression();
    }
    expect(TokenType::SEMICOLON, "expected ;");
    return std::make_unique<ReturnStmtNode>(std::move(value), start.line, start.column);
}

std::unique_ptr<ASTNode> Parser::parseExprStmt()
{
    auto expr = parseExpression();
    int line = expr->line;
    int column = expr->column;
    expect(TokenType::SEMICOLON, "expected ;");
    if (expr->type == ASTNodeType::ASSIGNMENT_STMT)
    {
        return expr;
    }
    return std::make_unique<ExpressionStmtNode>(std::move(expr), line, column);
}

std::unique_ptr<ASTNode> Parser::parseExpression()
{
    return parseAssignment();
}

std::unique_ptr<ASTNode> Parser::parseAssignment()
{
    auto left = parseLogicalOr();

    static const std::vector<TokenType> assignOps = {
        TokenType::ASSIGN, TokenType::PLUS_ASSIGN, TokenType::MINUS_ASSIGN,
        TokenType::STAR_ASSIGN, TokenType::SLASH_ASSIGN, TokenType::PERCENT_ASSIGN,
        TokenType::SHL_ASSIGN, TokenType::SHR_ASSIGN, TokenType::AMP_ASSIGN,
        TokenType::PIPE_ASSIGN, TokenType::CARET_ASSIGN
    };

    for (auto op : assignOps)
    {
        if (match(op))
        {
            auto value = parseAssignment();
            if (op == TokenType::ASSIGN)
            {
                return std::make_unique<AssignmentNode>(std::move(left), std::move(value), op,
                                                        left->line, left->column);
            }

            // 复合赋值 a += b 展开为 a = a + b
            BinaryOpType binOp;
            switch (op)
            {
                case TokenType::PLUS_ASSIGN: binOp = BinaryOpType::ADD; break;
                case TokenType::MINUS_ASSIGN: binOp = BinaryOpType::SUB; break;
                case TokenType::STAR_ASSIGN: binOp = BinaryOpType::MUL; break;
                case TokenType::SLASH_ASSIGN: binOp = BinaryOpType::DIV; break;
                case TokenType::PERCENT_ASSIGN: binOp = BinaryOpType::MOD; break;
                default: binOp = BinaryOpType::ADD; break;
            }

            auto targetCopy = left->type == ASTNodeType::VARIABLE_REF
                ? std::make_unique<VariableRefNode>(dynamic_cast<VariableRefNode*>(left.get())->name,
                                                    left->line, left->column)
                : nullptr;

            auto binExpr = std::make_unique<BinaryOpNode>(binOp, std::move(targetCopy), std::move(value));
            return std::make_unique<AssignmentNode>(std::move(left), std::move(binExpr), TokenType::ASSIGN,
                                                    left->line, left->column);
        }
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseLogicalOr()
{
    auto expr = parseLogicalAnd();
    while (match(TokenType::OR))
    {
        auto right = parseLogicalAnd();
        expr = std::make_unique<LogicalOpNode>(LogicalOpType::OR, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseLogicalAnd()
{
    auto expr = parseBitwiseOr();
    while (match(TokenType::AND))
    {
        auto right = parseBitwiseOr();
        expr = std::make_unique<LogicalOpNode>(LogicalOpType::AND, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseBitwiseOr()
{
    auto expr = parseBitwiseXor();
    while (match(TokenType::PIPE))
    {
        auto right = parseBitwiseXor();
        expr = std::make_unique<BinaryOpNode>(BinaryOpType::ADD, std::move(expr), std::move(right));
        // 简化：位或暂存为 BINARY_OP（后续细化）
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseBitwiseXor()
{
    auto expr = parseBitwiseAnd();
    while (match(TokenType::CARET))
    {
        auto right = parseBitwiseAnd();
        expr = std::make_unique<BinaryOpNode>(BinaryOpType::ADD, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseBitwiseAnd()
{
    auto expr = parseEquality();
    while (match(TokenType::AMP))
    {
        auto right = parseEquality();
        expr = std::make_unique<BinaryOpNode>(BinaryOpType::ADD, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseEquality()
{
    auto expr = parseRelational();
    while (true)
    {
        if (match(TokenType::EQ))
        {
            auto right = parseRelational();
            expr = std::make_unique<ComparisonOpNode>(ComparisonOpType::EQ, std::move(expr), std::move(right));
        }
        else if (match(TokenType::NE))
        {
            auto right = parseRelational();
            expr = std::make_unique<ComparisonOpNode>(ComparisonOpType::NE, std::move(expr), std::move(right));
        }
        else break;
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseRelational()
{
    auto expr = parseShift();
    while (true)
    {
        if (match(TokenType::LT))
        {
            auto right = parseShift();
            expr = std::make_unique<ComparisonOpNode>(ComparisonOpType::LT, std::move(expr), std::move(right));
        }
        else if (match(TokenType::LE))
        {
            auto right = parseShift();
            expr = std::make_unique<ComparisonOpNode>(ComparisonOpType::LE, std::move(expr), std::move(right));
        }
        else if (match(TokenType::GT))
        {
            auto right = parseShift();
            expr = std::make_unique<ComparisonOpNode>(ComparisonOpType::GT, std::move(expr), std::move(right));
        }
        else if (match(TokenType::GE))
        {
            auto right = parseShift();
            expr = std::make_unique<ComparisonOpNode>(ComparisonOpType::GE, std::move(expr), std::move(right));
        }
        else break;
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseShift()
{
    auto expr = parseAdditive();
    while (true)
    {
        if (match(TokenType::SHL))
        {
            auto right = parseAdditive();
            expr = std::make_unique<BinaryOpNode>(BinaryOpType::ADD, std::move(expr), std::move(right));
        }
        else if (match(TokenType::SHR))
        {
            auto right = parseAdditive();
            expr = std::make_unique<BinaryOpNode>(BinaryOpType::ADD, std::move(expr), std::move(right));
        }
        else break;
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseAdditive()
{
    auto expr = parseMultiplicative();
    while (true)
    {
        if (match(TokenType::PLUS))
        {
            auto right = parseMultiplicative();
            expr = std::make_unique<BinaryOpNode>(BinaryOpType::ADD, std::move(expr), std::move(right));
        }
        else if (match(TokenType::MINUS))
        {
            auto right = parseMultiplicative();
            expr = std::make_unique<BinaryOpNode>(BinaryOpType::SUB, std::move(expr), std::move(right));
        }
        else break;
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseMultiplicative()
{
    auto expr = parseUnary();
    while (true)
    {
        if (match(TokenType::STAR))
        {
            auto right = parseUnary();
            expr = std::make_unique<BinaryOpNode>(BinaryOpType::MUL, std::move(expr), std::move(right));
        }
        else if (match(TokenType::SLASH))
        {
            auto right = parseUnary();
            expr = std::make_unique<BinaryOpNode>(BinaryOpType::DIV, std::move(expr), std::move(right));
        }
        else if (match(TokenType::PERCENT))
        {
            auto right = parseUnary();
            expr = std::make_unique<BinaryOpNode>(BinaryOpType::MOD, std::move(expr), std::move(right));
        }
        else break;
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseUnary()
{
    if (match(TokenType::NOT))
    {
        auto operand = parseUnary();
        return std::make_unique<UnaryOpNode>(UnaryOpType::NOT, std::move(operand));
    }
    if (match(TokenType::MINUS))
    {
        auto operand = parseUnary();
        return std::make_unique<UnaryOpNode>(UnaryOpType::NEG, std::move(operand));
    }
    if (match(TokenType::INC))
    {
        auto operand = parseUnary();
        return std::make_unique<UnaryOpNode>(UnaryOpType::INC, std::move(operand));
    }
    if (match(TokenType::DEC))
    {
        auto operand = parseUnary();
        return std::make_unique<UnaryOpNode>(UnaryOpType::DEC, std::move(operand));
    }
    if (match(TokenType::STAR))
    {
        auto operand = parseUnary();
        return std::make_unique<UnaryOpNode>(UnaryOpType::NEG, std::move(operand));
    }
    if (match(TokenType::AMP))
    {
        auto operand = parseUnary();
        // 取地址：简化处理为 VariableRef 包装，后续可细化 AddressOfNode
        return operand;
    }
    if (match(TokenType::TILDE))
    {
        auto operand = parseUnary();
        return std::make_unique<UnaryOpNode>(UnaryOpType::NOT, std::move(operand));
    }
    return parsePostfix();
}

std::unique_ptr<ASTNode> Parser::parsePostfix()
{
    auto expr = parsePrimary();

    while (true)
    {
        if (match(TokenType::DOT))
        {
            // 成员访问 s.a.foo() —— 简化：拼接名字
            Token member = expect(TokenType::IDENT, "expected member name");
            std::string full = expr->type == ASTNodeType::VARIABLE_REF
                ? dynamic_cast<VariableRefNode*>(expr.get())->name + "." + member.text
                : member.text;
            expr = std::make_unique<VariableRefNode>(full, member.line, member.column);
        }
        else if (match(TokenType::LPAREN))
        {
            // 函数调用
            if (expr->type == ASTNodeType::VARIABLE_REF)
            {
                std::string name = dynamic_cast<VariableRefNode*>(expr.get())->name;
                auto args = parseArguments();
                expr = std::make_unique<FunctionCallNode>(name, std::move(args), expr->line, expr->column);
            }
            else
            {
                throw std::runtime_error("cannot call this expression");
            }
        }
        else if (match(TokenType::INC))
        {
            expr = std::make_unique<UnaryOpNode>(UnaryOpType::INC, std::move(expr));
        }
        else if (match(TokenType::DEC))
        {
            expr = std::make_unique<UnaryOpNode>(UnaryOpType::DEC, std::move(expr));
        }
        else break;
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parsePrimary()
{
    if (match(TokenType::NUMBER))
    {
        std::string text = previous().text;
        // 判断是否浮点
        bool isFloat = text.find('.') != std::string::npos;
        if (isFloat)
        {
            return std::make_unique<LiteralFloatNode>(std::stod(text), previous().line, previous().column);
        }
        // 处理进制前缀
        long long value;
        if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0)
        {
            value = std::stoll(text.substr(2), nullptr, 16);
        }
        else if (text.rfind("0b", 0) == 0 || text.rfind("0B", 0) == 0)
        {
            value = std::stoll(text.substr(2), nullptr, 2);
        }
        else if (text.rfind("0o", 0) == 0 || text.rfind("0O", 0) == 0)
        {
            value = std::stoll(text.substr(2), nullptr, 8);
        }
        else
        {
            value = std::stoll(text);
        }
        return std::make_unique<LiteralIntNode>(value, previous().line, previous().column);
    }
    if (match(TokenType::STRING))
    {
        return std::make_unique<LiteralStringNode>(previous().text, previous().line, previous().column);
    }
    if (match(TokenType::CHAR_LIT))
    {
        return std::make_unique<LiteralStringNode>(previous().text, previous().line, previous().column);
    }
    if (match(TokenType::TRUE))
    {
        return std::make_unique<LiteralBoolNode>(true, previous().line, previous().column);
    }
    if (match(TokenType::FALSE))
    {
        return std::make_unique<LiteralBoolNode>(false, previous().line, previous().column);
    }
    if (match(TokenType::IDENT))
    {
        return std::make_unique<VariableRefNode>(previous().text, previous().line, previous().column);
    }
    if (match(TokenType::THIS))
    {
        return std::make_unique<VariableRefNode>("this", previous().line, previous().column);
    }
    if (match(TokenType::LPAREN))
    {
        auto expr = parseExpression();
        expect(TokenType::RPAREN, "expected )");
        return expr;
    }
    throw std::runtime_error("syntax error: expected expression, got " + peek().text);
}

std::vector<std::unique_ptr<ASTNode>> Parser::parseArguments()
{
    std::vector<std::unique_ptr<ASTNode>> args;
    if (match(TokenType::RPAREN)) return args;
    do
    {
        if (match(TokenType::MOVE))
        {
            // move 传参：解析一个表达式，标记为移动（简化：直接解析）
        }
        args.push_back(parseExpression());
    } while (match(TokenType::COMMA));
    expect(TokenType::RPAREN, "expected )");
    return args;
}

std::vector<std::string> Parser::parseTypeParams()
{
    std::vector<std::string> params;
    do
    {
        std::string name = expect(TokenType::IDENT, "expected template parameter name").text;
        if (match(TokenType::COLON))
        {
            std::string kind = expect(TokenType::TYPE, "expected type").text;
            (void)kind;
        }
        params.push_back(name);
    } while (match(TokenType::COMMA));
    expect(TokenType::GT, "expected > to close template parameters");
    return params;
}
