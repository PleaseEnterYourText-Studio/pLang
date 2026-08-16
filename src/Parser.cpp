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
    genericPendingGT = false;

    // package 声明（可选但应在前）
    if (match(TokenType::PACKAGE))
    {
        auto pkgDecl = parsePackage();
        auto pkgNode = dynamic_cast<PackageStmtNode*>(pkgDecl.get());
        if (pkgNode) program->packageName = pkgNode->name;
    }

    while (!isAtEnd())
    {
        if (match(TokenType::SEMICOLON)) continue; // 跳过多余分号
        if (match(TokenType::IMPORT))
        {
            program->imports.push_back(parseImport());
        }
        else
        {
            try
            {
                auto decl = parseDeclaration();
                // 标注函数所属包（供跨包可见性检查与包限定调用解析使用）
                if (decl)
                {
                    if (auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get()))
                    {
                        fn->packageName = program->packageName;
                    }
                }
                if (decl) program->decls.push_back(std::move(decl));
            }
            catch (const std::runtime_error& e)
            {
                // 错误恢复：记录并跳到下一个顶层声明
                errors.push_back({errorLine, errorColumn, e.what()});
                while (!check(TokenType::SEMICOLON) && !isAtEnd()) advance();
                match(TokenType::SEMICOLON);
            }
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

Token Parser::peek(size_t offset) const
{
    size_t idx = pos + offset;
    if (idx >= tokens.size()) return tokens.back();
    return tokens[idx];
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

    // 若上一 token 与当前 token 不在同一行，错误定位到上一 token 末尾
    // （通常表示上一语句末尾缺少分号）
    if (pos > 0 && previous().line != peek().line)
    {
        errorLine = previous().line;
        errorColumn = previous().column + (int)previous().text.size();
        throw std::runtime_error(message + " but found end of line");
    }

    errorLine = peek().line;
    errorColumn = peek().column;
    throw std::runtime_error(message + " but got '" + peek().text + "'");
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

    return parseTypeSuffix();
}

// 类型（不含 var/val 修饰符前缀）
// 类型节点的文本形式（泛型参数名拼接用）
static std::string typeNodeText(TypeNode* t)
{
    if (!t) return "";
    switch (t->baseType)
    {
        case ASTNodeType::TYPE_I8: return "i8";
        case ASTNodeType::TYPE_I16: return "i16";
        case ASTNodeType::TYPE_I32: return "int";
        case ASTNodeType::TYPE_I64: return "i64";
        case ASTNodeType::TYPE_U8: return "u8";
        case ASTNodeType::TYPE_U16: return "u16";
        case ASTNodeType::TYPE_U32: return "uint";
        case ASTNodeType::TYPE_U64: return "u64";
        case ASTNodeType::TYPE_F32: return "f32";
        case ASTNodeType::TYPE_F64: return "f64";
        case ASTNodeType::TYPE_CHAR: return "char";
        case ASTNodeType::TYPE_BOOL: return "bool";
        case ASTNodeType::TYPE_STRING: return "string";
        case ASTNodeType::TYPE_POINTER: return "var -> var: " + typeNodeText(t->inner.get());
        case ASTNodeType::TYPE_ARRAY: return typeNodeText(t->inner.get()) + "[" + std::to_string(t->arraySize) + "]";
        default: return t->name;
    }
}

std::unique_ptr<TypeNode> Parser::parseTypeSuffix()
{
    auto typeNode = parsePrimitiveType();

    // 泛型实例化 Name<T1, T2>
    if (typeNode->baseType == ASTNodeType::TYPE_PRIMITIVE && check(TokenType::LT))
    {
        std::string gname = typeNode->name + "<";
        advance(); // <
        do
        {
            auto argTy = parseTypeSuffix();
            gname += typeNodeText(argTy.get());
            if (check(TokenType::COMMA)) gname += ",";
        } while (match(TokenType::COMMA));
        // 期望 >（嵌套泛型的 >> 会被词法拆成 SHR，这里拆回两个 >）
        if (genericPendingGT)
        {
            genericPendingGT = false;
        }
        else if (match(TokenType::GT)) { /* 已消费 */ }
        else if (match(TokenType::SHR))
        {
            genericPendingGT = true; // 剩下半个 > 留给外层
        }
        else
        {
            expect(TokenType::GT, "expected > to close generic arguments");
        }
        gname += ">";
        typeNode = std::make_unique<TypeNode>(ASTNodeType::TYPE_PRIMITIVE, gname,
                                              typeNode->line, typeNode->column);
    }

    // 后缀数组 T[n]
    while (match(TokenType::LBRACKET))
    {
        int size = 0;
        if (check(TokenType::NUMBER))
        {
            size = std::stoi(advance().text);
        }
        expect(TokenType::RBRACKET, "expected ] to close array length");
        typeNode = std::make_unique<TypeNode>(ASTNodeType::TYPE_ARRAY, "", peek().line, peek().column,
                                            size, std::move(typeNode));
    }
    return typeNode;
}

std::unique_ptr<TypeNode> Parser::parsePrimitiveType()
{
    Token tok = peek();
    std::string name = tok.text;

    ASTNodeType nodeType;
    switch (tok.type)
    {
        case TokenType::I8: nodeType = ASTNodeType::TYPE_I8; break;
        case TokenType::I16: nodeType = ASTNodeType::TYPE_I16; break;
        case TokenType::I32: nodeType = ASTNodeType::TYPE_I32; break;
        case TokenType::I64: nodeType = ASTNodeType::TYPE_I64; break;
        case TokenType::INT: nodeType = ASTNodeType::TYPE_I32; break;
        case TokenType::U8: nodeType = ASTNodeType::TYPE_U8; break;
        case TokenType::U16: nodeType = ASTNodeType::TYPE_U16; break;
        case TokenType::U32: nodeType = ASTNodeType::TYPE_U32; break;
        case TokenType::U64: nodeType = ASTNodeType::TYPE_U64; break;
        case TokenType::UINT: nodeType = ASTNodeType::TYPE_U32; break;
        case TokenType::F32: nodeType = ASTNodeType::TYPE_F32; break;
        case TokenType::F64: nodeType = ASTNodeType::TYPE_F64; break;
        case TokenType::CHAR: nodeType = ASTNodeType::TYPE_CHAR; break;
        case TokenType::FUNC: nodeType = ASTNodeType::TYPE_PRIMITIVE; name = "func"; break;
        case TokenType::STRING_TYPE: nodeType = ASTNodeType::TYPE_STRING; break;
        case TokenType::BOOL: nodeType = ASTNodeType::TYPE_BOOL; break;
        case TokenType::THIS_TYPE: nodeType = ASTNodeType::TYPE_TYPE; break;
        case TokenType::IDENT: nodeType = ASTNodeType::TYPE_PRIMITIVE; break;
        default:
            errorLine = tok.line;
            errorColumn = tok.column;
            throw std::runtime_error("expected type, got '" + tok.text + "'");
    }
    advance();
    return std::make_unique<TypeNode>(nodeType, name, tok.line, tok.column);
}

std::unique_ptr<ASTNode> Parser::parseDeclaration()
{
    // 顶层可见性修饰：pub func / pub struct（仅函数在 v1 生效）
    if (match(TokenType::PUB))
    {
        auto decl = parseDeclaration();
        if (auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get()))
        {
            fn->isPub = true;
        }
        return decl;
    }

    // extern FFI 声明：extern var stdin : ptr; / extern func ...
    if (match(TokenType::EXTERN))
    {
        if (match(TokenType::VAR) || match(TokenType::VAL))
        {
            // extern 全局数据
            bool isVar = previous().type == TokenType::VAR;
            Token gname = expect(TokenType::IDENT, "expected extern variable name");
            expect(TokenType::COLON, "expected : after extern variable name");
            auto gtype = parseTypeSuffix();
            expect(TokenType::SEMICOLON, "expected ;");
            return std::make_unique<ExternVarDeclNode>(gname.text, std::move(gtype), isVar,
                                                       gname.line, gname.column);
        }
        auto fn = parseFunctionDecl();
        if (auto* fnNode = dynamic_cast<FunctionDeclNode*>(fn.get()))
        {
            fnNode->isExtern = true;
        }
        return fn;
    }

    if (match(TokenType::USING)) return parseUsing();
    if (check(TokenType::FUNC)) return parseFunctionDecl();
    if (check(TokenType::STRUCT) || check(TokenType::ABSTRACT)) return parseStructDecl();
    if (check(TokenType::IMPL)) return parseImplDecl();

    errorLine = peek().line;
    errorColumn = peek().column;
    throw std::runtime_error("expected top-level declaration (using/func/struct/impl), got '" + peek().text + "'");
}

std::unique_ptr<ASTNode> Parser::parsePackage()
{
    Token name = expect(TokenType::IDENT, "expected package name");
    std::string pkgName = name.text;
    while (match(TokenType::DOT))
    {
        pkgName += "." + expect(TokenType::IDENT, "expected package path segment").text;
    }
    expect(TokenType::SEMICOLON, "expected ;");
    return std::make_unique<PackageStmtNode>(pkgName, name.line, name.column);
}

std::unique_ptr<ASTNode> Parser::parseImport()
{
    std::string path;
    Token first = expectPathSegment("expected import path");
    path = first.text;
    while (match(TokenType::DOT))
    {
        Token part = expectPathSegment("expected path segment");
        path += "." + part.text;
    }
    expect(TokenType::SEMICOLON, "expected ;");
    return std::make_unique<ImportStmtNode>(path, first.line, first.column);
}

Token Parser::expectPathSegment(const std::string& message)
{
    if (check(TokenType::IDENT) ||
        check(TokenType::INT) || check(TokenType::CHAR) || check(TokenType::STRING_TYPE) ||
        check(TokenType::WCHAR) || check(TokenType::WSTRING) || check(TokenType::BOOL) ||
        check(TokenType::I8) || check(TokenType::I16) || check(TokenType::I32) || check(TokenType::I64) ||
        check(TokenType::U8) || check(TokenType::U16) || check(TokenType::U32) || check(TokenType::U64) ||
        check(TokenType::UINT) || check(TokenType::F32) || check(TokenType::F64))
    {
        return advance();
    }
    return expect(TokenType::IDENT, message);
}

std::unique_ptr<ASTNode> Parser::parseUsing()
{
    Token name = expect(TokenType::IDENT, "expected alias name");
    expect(TokenType::ASSIGN, "expected =");
    std::unique_ptr<ASTNode> aliased;

    if (check(TokenType::STRUCT) || check(TokenType::ABSTRACT) || check(TokenType::UNION))
    {
        aliased = parseStructDecl(true);
        // 匿名结构体：把 using 的别名作为结构体名
        if (aliased->type == ASTNodeType::STRUCT_DECL)
        {
            auto* structNode = dynamic_cast<StructDeclNode*>(aliased.get());
            if (structNode->name.empty()) structNode->name = name.text;
        }
        expect(TokenType::SEMICOLON, "expected ; after using struct");
    }
    else
    {
        // using x = std.vector<i32>;
        std::string path = expect(TokenType::IDENT, "expected type path").text;
        while (match(TokenType::DOT))
        {
            path += "." + expect(TokenType::IDENT, "expected path segment").text;
        }
        std::unique_ptr<TypeNode> typeAlias;
        if (match(TokenType::LT))
        {
            // 泛型参数：解析第一个类型参数（简化）
            auto firstArg = parseType();
            while (match(TokenType::COMMA)) { parseType(); }
            expect(TokenType::GT, "expected > to close generics");
            typeAlias = std::make_unique<TypeNode>(ASTNodeType::TYPE_PRIMITIVE, path, name.line, name.column);
        }
        else
        {
            typeAlias = std::make_unique<TypeNode>(ASTNodeType::TYPE_PRIMITIVE, path, name.line, name.column);
        }
        expect(TokenType::SEMICOLON, "expected ;");
        aliased = std::move(typeAlias);
    }
    return std::make_unique<UsingDeclNode>(name.text, std::move(aliased), name.line, name.column);
}

std::unique_ptr<ASTNode> Parser::parseFunctionDecl()
{
    if (!match(TokenType::FUNC))
    {
        expect(TokenType::FUNC, "expected func");
    }
    // 特殊方法名（.construction/.destroy/.copy）：点号前缀
    std::string specialPrefix;
    if (match(TokenType::DOT))
    {
        specialPrefix = ".";
    }
    Token name = expect(TokenType::IDENT, "expected function name");
    if (!specialPrefix.empty())
    {
        name.text = specialPrefix + name.text;
    }

    // 模板参数 <T: type> 或 <a: int>
    std::vector<std::string> typeParams;
    if (match(TokenType::LT))
    {
        typeParams = parseTypeParams();
    }

    auto fnDecl = std::make_unique<FunctionDeclNode>(name.text, name.line, name.column);
    fnDecl->typeParams = typeParams;

    expect(TokenType::LPAREN, "expected (");
    if (!check(TokenType::RPAREN))
    {
        do
        {
            // 变参标记 ...（仅 extern 声明有意义）
            if (check(TokenType::DOT) && peek(1).type == TokenType::DOT && peek(2).type == TokenType::DOT)
            {
                advance(); advance(); advance();
                fnDecl->isVariadic = true;
                break;
            }
            bool isVar = true;
            if (match(TokenType::VAL)) isVar = false;
            else if (match(TokenType::VAR)) isVar = true;
            else
            {
                errorLine = peek().line;
                errorColumn = peek().column;
                throw std::runtime_error("expected parameter modifier val/var");
            }

            std::unique_ptr<TypeNode> paramType;
            if (check(TokenType::ARROW))
            {
                // 指针参数：var -> var[: T] name
                advance();
                if (match(TokenType::VAR)) { /* rw */ }
                else if (match(TokenType::VAL)) { /* const */ }
                std::unique_ptr<TypeNode> inner;
                if (match(TokenType::COLON))
                {
                    inner = parseTypeSuffix();
                }
                paramType = std::make_unique<TypeNode>(ASTNodeType::TYPE_POINTER, "", peek().line, peek().column,
                                                       0, std::move(inner), false);
            }
            else
            {
                expect(TokenType::COLON, "expected : after parameter modifier");
                paramType = parseTypeSuffix();
            }

            Token pname = expect(TokenType::IDENT, "expected parameter name");
            fnDecl->params.push_back(std::make_unique<ParameterNode>(isVar, pname.text, std::move(paramType),
                                                                 pname.line, pname.column));
        } while (match(TokenType::COMMA));
    }
    expect(TokenType::RPAREN, "expected )");

    // 返回类型（与变量声明统一）：
    //   func foo() : T          返回 T（值，与 var: T a 的写法一致）
    //   func foo() -> var T     返回 T 指针（箭头 + var/val 修饰，兼容 -> var: T）
    //   func foo() -> T         旧语法：返回 T（值），保留兼容
    if (match(TokenType::COLON))
    {
        fnDecl->returnType = parseType();
    }
    else if (match(TokenType::ARROW))
    {
        if (check(TokenType::VAR) || check(TokenType::VAL))
        {
            // 指针返回：-> var T
            bool innerConst = false;
            if (match(TokenType::VAR)) { /* rw */ }
            else if (match(TokenType::VAL)) { innerConst = true; }

            std::unique_ptr<TypeNode> inner;
            if (match(TokenType::COLON))
            {
                inner = parseTypeSuffix();
            }
            else
            {
                inner = parsePrimitiveType();
            }
            fnDecl->returnType = std::make_unique<TypeNode>(
                ASTNodeType::TYPE_POINTER, "", peek().line, peek().column,
                0, std::move(inner), innerConst);
        }
        else
        {
            fnDecl->returnType = parseType(); // 旧语法兼容：值返回
        }
    }

    if (match(TokenType::SEMICOLON))
    {
        fnDecl->hasBody = false; // 仅声明，由 impl 实现
        return fnDecl;
    }

    fnDecl->hasBody = true;
    fnDecl->body = std::make_unique<BlockStmtNode>(peek().line, peek().column);
    auto block = parseBlock();
    fnDecl->body = std::unique_ptr<BlockStmtNode>(dynamic_cast<BlockStmtNode*>(block.release()));
    return fnDecl;
}

std::unique_ptr<ASTNode> Parser::parseStructDecl(bool allowAnonymous)
{
    bool isAbstract = match(TokenType::ABSTRACT);
    bool isUnion = false;
    if (check(TokenType::STRUCT))
    {
        advance();
    }
    else if (check(TokenType::UNION))
    {
        advance();
        isUnion = true;
    }
    else if (!isAbstract)
    {
        expect(TokenType::STRUCT, "expected struct");
    }

    // 可选对齐 align(N)
    int alignBytes = 0;
    if (match(TokenType::ALIGN))
    {
        expect(TokenType::LPAREN, "expected ( after align");
        alignBytes = std::stoi(expect(TokenType::NUMBER, "expected alignment value").text);
        expect(TokenType::RPAREN, "expected ) after alignment value");
    }

    std::string structName;
    int line = peek().line;
    int column = peek().column;
    if (allowAnonymous && (check(TokenType::LBRACE) || check(TokenType::COLON) || check(TokenType::LT)))
    {
        structName = "";
    }
    else
    {
        Token name = expect(TokenType::IDENT, "expected struct name");
        structName = name.text;
        line = name.line;
        column = name.column;
    }
    auto structNode = std::make_unique<StructDeclNode>(structName, isAbstract, line, column);
    structNode->isUnion = isUnion;
    structNode->alignBytes = alignBytes;

    // 泛型 <T: type>
    if (match(TokenType::LT))
    {
        structNode->typeParams = parseTypeParams();
    }

    // 继承 : pub A, B
    if (match(TokenType::COLON))
    {
        do
        {
            match(TokenType::PUB);
            structNode->bases.push_back(expect(TokenType::IDENT, "expected base class name").text);
        } while (match(TokenType::COMMA));
    }

    expect(TokenType::LBRACE, "expected {");
    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        if (match(TokenType::SEMICOLON)) continue; // 跳过多余分号
        // 访问权限修饰符 pub/prt/pri（可选）
        matchAny({TokenType::PUB, TokenType::PRT, TokenType::PRI});
        if (check(TokenType::FUNC))
        {
            structNode->members.push_back(parseFunctionDecl());
        }
        else if (check(TokenType::DOT))
        {
            // .特殊函数（构造/析构等），先跳过点号再解析
            advance();
            structNode->members.push_back(parseFunctionDecl());
        }
        else
        {
            structNode->members.push_back(parseStatement());
        }
    }
    expect(TokenType::RBRACE, "expected }");

    return structNode;
}

std::unique_ptr<ASTNode> Parser::parseImplDecl()
{
    expect(TokenType::IMPL, "expected impl");
    std::string target = expect(TokenType::IDENT, "expected target name").text;
    while (match(TokenType::DOT))
    {
        target += "." + expect(TokenType::IDENT, "expected member name").text;
    }

    auto implNode = std::make_unique<ImplDeclNode>(target, peek().line, peek().column);
    expect(TokenType::LBRACE, "expected {");
    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        // 方法成员：pub/prt/pri 权限 + func
        matchAny({TokenType::PUB, TokenType::PRT, TokenType::PRI});
        if (check(TokenType::FUNC))
        {
            implNode->members.push_back(parseFunctionDecl());
        }
        else
        {
            implNode->members.push_back(parseStatement());
        }
    }
    expect(TokenType::RBRACE, "expected }");
    return implNode;
}

std::unique_ptr<ASTNode> Parser::parseStatement()
{
    if (check(TokenType::VAR) || check(TokenType::VAL) || check(TokenType::VOLATILE)) return parseVarDecl();
    if (check(TokenType::IF)) return parseIf();
    if (check(TokenType::WHILE)) return parseWhile();
    if (check(TokenType::FOR)) return parseFor();
    if (check(TokenType::DO)) return parseDoWhile();
    if (check(TokenType::GOTO)) return parseGoto();
    if (check(TokenType::LABEL)) return parseLabel();
    if (check(TokenType::SWITCH)) return parseSwitch();
    if (check(TokenType::RETURN)) return parseReturn();
    if (check(TokenType::ASM)) return parseAsm();
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
        try
        {
            auto stmt = parseStatement();
            if (stmt) block->statements.push_back(std::move(stmt));
        }
        catch (const std::runtime_error& e)
        {
            // 错误恢复：记录并同步到语句结束，继续解析后续语句
            errors.push_back({errorLine, errorColumn, e.what()});
            while (!check(TokenType::SEMICOLON) && !check(TokenType::RBRACE) && !isAtEnd()) advance();
            match(TokenType::SEMICOLON);
        }
    }
    expect(TokenType::RBRACE, "expected }");
    return block;
}

std::unique_ptr<ASTNode> Parser::parseAsm()
{
    Token start = expect(TokenType::ASM, "expected asm");
    expect(TokenType::LBRACE, "expected { after asm");

    auto asmNode = std::make_unique<AsmNode>("", start.line, start.column);

    // 模板字符串
    Token tmpl = expect(TokenType::STRING, "expected asm template string");
    asmNode->template_str = tmpl.text;

    // 输出操作数 : "=r"(name), ...
    if (match(TokenType::COLON))
    {
        if (!check(TokenType::COLON) && !check(TokenType::RBRACE))
        {
            do
            {
                Token constraint = expect(TokenType::STRING, "expected constraint string");
                expect(TokenType::LPAREN, "expected ( after constraint");
                Token name = expect(TokenType::IDENT, "expected operand name");
                expect(TokenType::RPAREN, "expected )");
                asmNode->outputs.push_back({constraint.text, name.text});
            } while (match(TokenType::COMMA));
        }

        // 输入操作数 : "r"(name), ...
        if (match(TokenType::COLON))
        {
            if (!check(TokenType::COLON) && !check(TokenType::RBRACE))
            {
                do
                {
                    Token constraint = expect(TokenType::STRING, "expected constraint string");
                    expect(TokenType::LPAREN, "expected ( after constraint");
                    Token name = expect(TokenType::IDENT, "expected operand name");
                    expect(TokenType::RPAREN, "expected )");
                    asmNode->inputs.push_back({constraint.text, name.text});
                } while (match(TokenType::COMMA));
            }

            // clobber 列表 : "cc", ...
            if (match(TokenType::COLON))
            {
                if (!check(TokenType::RBRACE))
                {
                    do
                    {
                        Token clob = expect(TokenType::STRING, "expected clobber string");
                        asmNode->clobbers.push_back(clob.text);
                    } while (match(TokenType::COMMA));
                }
            }
        }
    }

    expect(TokenType::RBRACE, "expected } to close asm");
    return asmNode;
}

std::unique_ptr<ASTNode> Parser::parseVarDecl()
{
    bool isVolatile = false;
    if (match(TokenType::VOLATILE)) isVolatile = true;
    bool isVar;
    if (match(TokenType::VAL)) isVar = false;
    else if (match(TokenType::VAR)) isVar = true;
    else
    {
        errorLine = peek().line;
        errorColumn = peek().column;
        throw std::runtime_error("expected var/val");
    }

    bool isMoved = false;
    std::unique_ptr<TypeNode> type;
    std::string name;
    std::unique_ptr<ASTNode> init;

    // 指针类型: var -> var: T p
    if (check(TokenType::ARROW))
    {
        advance(); // 消费 ->
        bool innerConst = false;
        if (match(TokenType::VAR)) { /* rw */ }
        else if (match(TokenType::VAL)) { innerConst = true; }
        std::unique_ptr<TypeNode> inner;
        if (match(TokenType::COLON))
        {
            inner = parseTypeSuffix();
        }
        else
        {
            inner = parsePrimitiveType();
        }
        type = std::make_unique<TypeNode>(ASTNodeType::TYPE_POINTER, "", peek().line, peek().column,
                                          0, std::move(inner), innerConst);
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

    // 位域宽度: var: int x: 3;
    int bitWidth = 0;
    if (match(TokenType::COLON))
    {
        bitWidth = std::stoi(expect(TokenType::NUMBER, "expected bit width").text);
    }

    // 引用声明: var ref@target
    if (match(TokenType::AT))
    {
        std::string target = expect(TokenType::IDENT, "expected reference target").text;
        init = std::make_unique<VariableRefNode>(target, previous().line, previous().column);
    }

    if (match(TokenType::ASSIGN))
    {
        if (match(TokenType::MOVE))
        {
            isMoved = true;
        }
        if (check(TokenType::LBRACE))
        {
            // 结构体/数组初始化 {1, 2, 3}
            init = parseInitList();
        }
        else
        {
            init = parseExpression();
        }
    }

    expect(TokenType::SEMICOLON, "expected ;");
    return std::make_unique<VariableDeclNode>(isVar, isMoved, name, std::move(type), std::move(init),
                                              previous().line, previous().column, bitWidth, isVolatile);
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

std::unique_ptr<ASTNode> Parser::parseGoto()
{
    Token start = expect(TokenType::GOTO, "expected goto");
    Token name = expect(TokenType::IDENT, "expected label name");
    expect(TokenType::SEMICOLON, "expected ; after goto");
    return std::make_unique<GotoStmtNode>(name.text, start.line, start.column);
}

std::unique_ptr<ASTNode> Parser::parseLabel()
{
    Token start = expect(TokenType::LABEL, "expected label");
    Token name = expect(TokenType::IDENT, "expected label name");
    expect(TokenType::SEMICOLON, "expected ; after label");
    return std::make_unique<LabelStmtNode>(name.text, start.line, start.column);
}

std::unique_ptr<ASTNode> Parser::parseSwitch()
{
    Token start = expect(TokenType::SWITCH, "expected switch");
    expect(TokenType::LPAREN, "expected ( after switch");
    auto cond = parseExpression();
    expect(TokenType::RPAREN, "expected )");
    expect(TokenType::LBRACE, "expected {");
    auto sw = std::make_unique<SwitchStmtNode>(std::move(cond), start.line, start.column);

    while (!check(TokenType::RBRACE) && !isAtEnd())
    {
        SwitchCase c;
        if (match(TokenType::CASE))
        {
            Token v = expect(TokenType::NUMBER, "expected case value");
            c.value = std::stoll(v.text);
        }
        else if (match(TokenType::DEFAULT))
        {
            c.isDefault = true;
        }
        else
        {
            errorLine = peek().line;
            errorColumn = peek().column;
            throw std::runtime_error("expected case or default in switch");
        }
        expect(TokenType::COLON, "expected : after case/default");
        auto body = std::make_unique<BlockStmtNode>(previous().line, previous().column);
        while (!check(TokenType::CASE) && !check(TokenType::DEFAULT) &&
               !check(TokenType::RBRACE) && !isAtEnd())
        {
            auto stmt = parseStatement();
            if (stmt) body->statements.push_back(std::move(stmt));
        }
        c.body = std::move(body);
        sw->cases.push_back(std::move(c));
    }
    expect(TokenType::RBRACE, "expected } after switch");
    return sw;
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
    // 类型转换: TargetType as expr
    if (check(TokenType::INT) || check(TokenType::I8) || check(TokenType::I16) ||
        check(TokenType::I32) || check(TokenType::I64) || check(TokenType::U8) ||
        check(TokenType::U16) || check(TokenType::U32) || check(TokenType::U64) ||
        check(TokenType::UINT) || check(TokenType::F32) || check(TokenType::F64) ||
        check(TokenType::CHAR) || check(TokenType::STRING_TYPE) || check(TokenType::BOOL))
    {
        // 先看看是否是指针类型 var/val -> ... 或普通 as 转换
        // 尝试匹配 as: int as expr
        if (peek(1).type == TokenType::AS)
        {
            std::string targetType = advance().text;
            int line = previous().line;
            int column = previous().column;
            advance(); // 消费 as
            auto value = parseExpression();
            return std::make_unique<CastNode>(targetType, std::move(value), line, column);
        }
    }
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
            auto value = parseExpression(); // 走 parseExpression 以支持 RHS 上的类型转换
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
                case TokenType::AMP_ASSIGN: binOp = BinaryOpType::BITAND; break;
                case TokenType::PIPE_ASSIGN: binOp = BinaryOpType::BITOR; break;
                case TokenType::CARET_ASSIGN: binOp = BinaryOpType::BITXOR; break;
                case TokenType::SHL_ASSIGN: binOp = BinaryOpType::SHL; break;
                case TokenType::SHR_ASSIGN: binOp = BinaryOpType::SHR; break;
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
        expr = std::make_unique<BinaryOpNode>(BinaryOpType::BITOR, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseBitwiseXor()
{
    auto expr = parseBitwiseAnd();
    while (match(TokenType::CARET))
    {
        auto right = parseBitwiseAnd();
        expr = std::make_unique<BinaryOpNode>(BinaryOpType::BITXOR, std::move(expr), std::move(right));
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseBitwiseAnd()
{
    auto expr = parseEquality();
    while (match(TokenType::AMP))
    {
        auto right = parseEquality();
        expr = std::make_unique<BinaryOpNode>(BinaryOpType::BITAND, std::move(expr), std::move(right));
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
            expr = std::make_unique<BinaryOpNode>(BinaryOpType::SHL, std::move(expr), std::move(right));
        }
        else if (match(TokenType::SHR))
        {
            auto right = parseAdditive();
            expr = std::make_unique<BinaryOpNode>(BinaryOpType::SHR, std::move(expr), std::move(right));
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
    // 类型转换: TYPE as expr（作为操作数出现时，如 v - int as x）
    if (check(TokenType::INT) || check(TokenType::I8) || check(TokenType::I16) ||
        check(TokenType::I32) || check(TokenType::I64) || check(TokenType::U8) ||
        check(TokenType::U16) || check(TokenType::U32) || check(TokenType::U64) ||
        check(TokenType::UINT) || check(TokenType::F32) || check(TokenType::F64) ||
        check(TokenType::CHAR) || check(TokenType::STRING_TYPE) || check(TokenType::BOOL))
    {
        if (peek(1).type == TokenType::AS)
        {
            std::string targetType = advance().text;
            int line = previous().line;
            int column = previous().column;
            advance(); // 消费 as
            auto value = parseExpression();
            return std::make_unique<CastNode>(targetType, std::move(value), line, column);
        }
    }
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
        return std::make_unique<UnaryOpNode>(UnaryOpType::DEREF, std::move(operand));
    }
    if (match(TokenType::AMP))
    {
        auto operand = parseUnary();
        return std::make_unique<AddressOfNode>(std::move(operand), previous().line, previous().column);
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
        else if (match(TokenType::LBRACKET))
        {
            // 数组下标 buf[i]
            auto index = parseExpression();
            expect(TokenType::RBRACKET, "expected ]");
            expr = std::make_unique<IndexNode>(std::move(expr), std::move(index), index->line, index->column);
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
                {
                    errorLine = peek().line;
                    errorColumn = peek().column;
                    throw std::runtime_error("cannot call this expression");
                }
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
    if (match(TokenType::LBRACE))
    {
        // 嵌套初始化列表 {{1,2},3} —— 与 parseInitList 结构一致
        auto block = std::make_unique<BlockStmtNode>(previous().line, previous().column);
        if (!check(TokenType::RBRACE))
        {
            do
            {
                block->statements.push_back(parseExpression());
            } while (match(TokenType::COMMA));
        }
        expect(TokenType::RBRACE, "expected }");
        return block;
    }
    if (match(TokenType::NUMBER))
    {
        std::string text = previous().text;
        // 判断是否浮点
        bool isFloat = text.find('.') != std::string::npos;
        if (isFloat)
        {
            // 提取后缀 (f/F)
            std::string numPart = text;
            std::string suffix;
            if (!numPart.empty() && (numPart.back() == 'f' || numPart.back() == 'F'))
            {
                suffix = "f";
                numPart.pop_back();
            }
            return std::make_unique<LiteralFloatNode>(std::stod(numPart), previous().line, previous().column, suffix);
        }
        // 处理进制前缀
        long long value;
        std::string suffix;
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
            // 提取整数后缀 ll/L
            std::string numPart = text;
            if (numPart.size() >= 2 && (numPart.compare(numPart.size() - 2, 2, "ll") == 0))
            {
                suffix = "ll";
                numPart = numPart.substr(0, numPart.size() - 2);
            }
            else if (!numPart.empty() && (numPart.back() == 'l' || numPart.back() == 'L'))
            {
                suffix = "l";
                numPart.pop_back();
            }
            value = std::stoll(numPart);
        }
        return std::make_unique<LiteralIntNode>(value, previous().line, previous().column, suffix);
    }
    if (match(TokenType::STRING))
    {
        return std::make_unique<LiteralStringNode>(previous().text, previous().line, previous().column);
    }
    if (match(TokenType::CHAR_LIT))
    {
        return std::make_unique<LiteralStringNode>(previous().text, previous().line, previous().column, true);
    }
    if (match(TokenType::TRUE))
    {
        return std::make_unique<LiteralBoolNode>(true, previous().line, previous().column);
    }
    if (match(TokenType::FALSE))
    {
        return std::make_unique<LiteralBoolNode>(false, previous().line, previous().column);
    }
    if (match(TokenType::NULL_LIT))
    {
        return std::make_unique<NullNode>(previous().line, previous().column);
    }
    if (match(TokenType::IDENT))
    {
        std::string name = previous().text;
        // 泛型函数调用 foo<T1,T2>(...)：IDENT 后紧跟 < 时尝试解析泛型实参（失败则回退为普通标识符）
        if (check(TokenType::LT))
        {
            size_t savePos = pos;
            bool savedGT = genericPendingGT;
            try
            {
                advance(); // <
                std::string gname = name + "<";
                do
                {
                    auto argTy = parseTypeSuffix();
                    gname += typeNodeText(argTy.get());
                    if (check(TokenType::COMMA)) gname += ",";
                } while (match(TokenType::COMMA));
                if (genericPendingGT)
                {
                    genericPendingGT = false;
                }
                else if (match(TokenType::GT)) { /* 已消费 */ }
                else if (match(TokenType::SHR))
                {
                    genericPendingGT = true;
                }
                else
                {
                    throw std::runtime_error("generic call");
                }
                gname += ">";
                // 仅当后面是 ( 才当作泛型调用，否则回退（可能是 a < b 比较）
                if (check(TokenType::LPAREN))
                {
                    name = gname;
                }
                else
                {
                    pos = savePos;
                    genericPendingGT = savedGT;
                }
            }
            catch (...)
            {
                pos = savePos;
                genericPendingGT = savedGT;
            }
        }
        return std::make_unique<VariableRefNode>(name, previous().line, previous().column);
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
    errorLine = peek().line;
    errorColumn = peek().column;
    throw std::runtime_error("expected expression, got '" + peek().text + "'");
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

std::unique_ptr<ASTNode> Parser::parseInitList()
{
    expect(TokenType::LBRACE, "expected {");
    auto block = std::make_unique<BlockStmtNode>(previous().line, previous().column);
    if (!check(TokenType::RBRACE))
    {
        do
        {
            block->statements.push_back(parseExpression());
        } while (match(TokenType::COMMA));
    }
    expect(TokenType::RBRACE, "expected }");
    return block;
}
