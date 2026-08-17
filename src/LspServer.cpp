#include "../include/LspServer.h"
#include "../include/Lexer.h"
#include "../include/Parser.h"
#include "../include/Sema.h"
#include "../include/Importer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// 序列化 JSON 值
static std::string jsonToString(const llvm::json::Value& v)
{
    std::string s;
    llvm::raw_string_ostream os(s);
    os << v;
    os.flush();
    return s;
}

// ============ 消息读写（stdio 上的 Content-Length 帧） ============

std::string LspServer::readMessage()
{
    std::string line;
    int contentLength = -1;

    // 读取头
    while (std::getline(std::cin, line))
    {
        if (line.empty() || line == "\r") break;
        size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            std::string key = line.substr(0, colon);
            if (key == "Content-Length")
            {
                contentLength = std::stoi(line.substr(colon + 1));
            }
        }
    }

    if (contentLength < 0) return "";

    std::string body(contentLength, '\0');
    std::cin.read(&body[0], contentLength);
    return body;
}

void LspServer::sendResponse(int id, const llvm::json::Value& result)
{
    llvm::json::Object obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = id;
    obj["result"] = result;
    std::string body = jsonToString(llvm::json::Value(std::move(obj)));
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

void LspServer::sendNotification(const std::string& method, const llvm::json::Value& params)
{
    llvm::json::Object obj;
    obj["jsonrpc"] = "2.0";
    obj["method"] = method;
    obj["params"] = params;
    std::string body = jsonToString(llvm::json::Value(std::move(obj)));
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

llvm::json::Value LspServer::makeError(int code, const std::string& message)
{
    llvm::json::Object err;
    err["code"] = code;
    err["message"] = message;
    return llvm::json::Value(std::move(err));
}

// ============ 文档管理 ============

LspPosition LspServer::offsetToPosition(const DocumentState& doc, int offset) const
{
    int line = 0, col = 0;
    for (int i = 0; i < offset && i < (int)doc.text.size(); ++i)
    {
        if (doc.text[i] == '\n') { line++; col = 0; }
        else col++;
    }
    return {line, col};
}

void LspServer::analyzeDocument(DocumentState& doc)
{
    doc.symbols.clear();
    doc.errors.clear();
    doc.warnings.clear();
    doc.parsed = false;

    Lexer lexer(doc.text);
    auto tokens = lexer.scanTokens();
    for (const auto& tok : tokens)
    {
        if (tok.type == TokenType::ERROR)
        {
            doc.errors.emplace_back(tok.line, tok.column, tok.text);
        }
    }

    Parser parser(tokens);
    try
    {
        doc.program = parser.parse();
    }
    catch (...)
    {
        doc.errors.emplace_back(parser.getErrorLine(), parser.getErrorColumn(), "syntax error");
    }

    // 即使语法错误，也尽量收集已解析的部分符号（保证高亮不退化）
    if (doc.program)
    {
        // 解析 import：合并标准库声明（编辑器内库函数可用）
        size_t ownDeclCount = doc.program->decls.size();
        bool importError = false;
        plangResolveImports(doc.program.get(), plangGetStdlibRoot(""), importError);

        Sema sema;
        if (!sema.analyze(doc.program))
        {
            doc.errors = sema.getErrors();
        }
        doc.warnings = sema.getWarnings();
        // 大纲只收集本文件声明的符号
        for (size_t i = 0; i < ownDeclCount; ++i)
        {
            collectSymbols(doc, doc.program->decls[i].get());
        }
        doc.parsed = true;
    }
    else
    {
        // 语法错误时用 Lexer 兜底：识别 func/using/struct 后的名字
        for (size_t i = 0; i + 1 < tokens.size(); ++i)
        {
            const auto& t = tokens[i];
            if (t.type == TokenType::FUNC && tokens[i + 1].type == TokenType::IDENT)
            {
                SymbolInfo info;
                info.name = tokens[i + 1].text;
                info.uri = doc.uri;
                int ln = tokens[i + 1].line - 1, col = tokens[i + 1].column - 1;
                info.range = {{ln, col}, {ln, col + (int)info.name.size()}};
                info.selectionRange = info.range;
                info.kind = "function";
                doc.symbols.push_back(info);
            }
            else if (t.type == TokenType::USING && tokens[i + 1].type == TokenType::IDENT)
            {
                SymbolInfo info;
                info.name = tokens[i + 1].text;
                info.uri = doc.uri;
                int ln = tokens[i + 1].line - 1, col = tokens[i + 1].column - 1;
                info.range = {{ln, col}, {ln, col + (int)info.name.size()}};
                info.selectionRange = info.range;
                info.kind = "type";
                doc.symbols.push_back(info);
            }
        }
    }
}

void LspServer::collectSymbols(DocumentState& doc, ASTNode* node)
{
    if (!node) return;

    switch (node->type)
    {
        case ASTNodeType::FUNCTION_DECL:
        {
            auto* fn = static_cast<FunctionDeclNode*>(node);
            SymbolInfo info;
            info.name = fn->name;
            info.uri = doc.uri;
            info.range = {{fn->line - 1, fn->column - 1},
                          {fn->line - 1, fn->column - 1 + (int)fn->name.size()}};
            info.selectionRange = info.range;
            info.kind = "function";
            if (fn->returnType)
            {
                switch (fn->returnType->baseType)
                {
                    case ASTNodeType::TYPE_I32: info.typeName = "int"; break;
                    case ASTNodeType::TYPE_I64: info.typeName = "i64"; break;
                    case ASTNodeType::TYPE_F64: info.typeName = "f64"; break;
                    case ASTNodeType::TYPE_CHAR: info.typeName = "char"; break;
                    case ASTNodeType::TYPE_BOOL: info.typeName = "bool"; break;
                    case ASTNodeType::TYPE_PRIMITIVE: info.typeName = fn->returnType->name; break;
                    default: info.typeName = "?"; break;
                }
            }
            doc.symbols.push_back(info);
            break;
        }
        case ASTNodeType::STRUCT_DECL:
        {
            auto* st = static_cast<StructDeclNode*>(node);
            SymbolInfo info;
            info.name = st->name;
            info.uri = doc.uri;
            info.range = {{st->line - 1, st->column - 1},
                          {st->line - 1, st->column - 1 + (int)st->name.size()}};
            info.selectionRange = info.range;
            info.kind = "type";
            doc.symbols.push_back(info);
            break;
        }
        case ASTNodeType::VARIABLE_DECL:
        {
            auto* v = static_cast<VariableDeclNode*>(node);
            SymbolInfo info;
            info.name = v->name;
            info.uri = doc.uri;
            info.range = {{v->line - 1, v->column - 1},
                          {v->line - 1, v->column - 1 + (int)v->name.size()}};
            info.selectionRange = info.range;
            // ParameterNode 复用 VARIABLE_DECL 类型，用 dynamic_cast 区分
            if (dynamic_cast<ParameterNode*>(node))
                info.kind = "parameter";
            else
                info.kind = (v->isVar ? "variable" : "constant");
            if (v->type)
            {
                switch (v->type->baseType)
                {
                    case ASTNodeType::TYPE_I32: info.typeName = "int"; break;
                    case ASTNodeType::TYPE_I64: info.typeName = "i64"; break;
                    case ASTNodeType::TYPE_F32: info.typeName = "f32"; break;
                    case ASTNodeType::TYPE_F64: info.typeName = "f64"; break;
                    case ASTNodeType::TYPE_CHAR: info.typeName = "char"; break;
                    case ASTNodeType::TYPE_BOOL: info.typeName = "bool"; break;
                    case ASTNodeType::TYPE_PRIMITIVE: info.typeName = v->type->name; break;
                    case ASTNodeType::TYPE_POINTER: info.typeName = "pointer"; break;
                    default: info.typeName = "?"; break;
                }
            }
            doc.symbols.push_back(info);
            break;
        }
        default:
            break;
    }

    // 递归遍历子节点
    switch (node->type)
    {
        case ASTNodeType::PROGRAM:
        {
            auto* p = static_cast<ProgramNode*>(node);
            for (auto& d : p->decls) collectSymbols(doc, d.get());
            break;
        }
        case ASTNodeType::FUNCTION_DECL:
        {
            auto* fn = static_cast<FunctionDeclNode*>(node);
            for (auto& prm : fn->params) collectSymbols(doc, prm.get());
            if (fn->body) collectSymbols(doc, fn->body.get());
            break;
        }
        case ASTNodeType::BLOCK_STMT:
        {
            auto* b = static_cast<BlockStmtNode*>(node);
            for (auto& s : b->statements) collectSymbols(doc, s.get());
            break;
        }
        case ASTNodeType::STRUCT_DECL:
        {
            auto* st = static_cast<StructDeclNode*>(node);
            for (auto& m : st->members) collectSymbols(doc, m.get());
            break;
        }
        case ASTNodeType::IF_STMT:
        {
            auto* i = static_cast<IfStmtNode*>(node);
            if (i->thenBranch) collectSymbols(doc, i->thenBranch.get());
            if (i->elseBranch) collectSymbols(doc, i->elseBranch.get());
            break;
        }
        case ASTNodeType::WHILE_STMT:
        {
            auto* w = static_cast<WhileStmtNode*>(node);
            if (w->body) collectSymbols(doc, w->body.get());
            break;
        }
        case ASTNodeType::FOR_STMT:
        {
            auto* f = static_cast<ForStmtNode*>(node);
            if (f->init) collectSymbols(doc, f->init.get());
            if (f->body) collectSymbols(doc, f->body.get());
            break;
        }
        case ASTNodeType::VARIABLE_DECL:
        {
            auto* v = static_cast<VariableDeclNode*>(node);
            if (v->initializer) collectSymbols(doc, v->initializer.get());
            break;
        }
        case ASTNodeType::RETURN_STMT:
        {
            auto* r = static_cast<ReturnStmtNode*>(node);
            if (r->value) collectSymbols(doc, r->value.get());
            break;
        }
        default:
            break;
    }
}

void LspServer::openDocument(const std::string& uri, const std::string& text)
{
    auto doc = std::make_shared<DocumentState>();
    doc->uri = uri;
    doc->text = text;
    documents[uri] = doc;
    analyzeDocument(*doc);

    // 推送诊断
    llvm::json::Array diagnostics;
    for (const auto& err : doc->errors)
    {
        llvm::json::Object diag;
        diag["range"] = llvm::json::Object{
            {"start", llvm::json::Object{{"line", err.line - 1}, {"character", err.column - 1}}},
            {"end", llvm::json::Object{{"line", err.line - 1}, {"character", err.column - 1}}},
        };
        diag["severity"] = 1;
        diag["message"] = err.message;
        diagnostics.push_back(llvm::json::Value(std::move(diag)));
    }
    llvm::json::Object params;
    params["uri"] = uri;
    params["diagnostics"] = llvm::json::Value(std::move(diagnostics));
    sendNotification("textDocument/publishDiagnostics", llvm::json::Value(std::move(params)));
}

void LspServer::updateDocument(const std::string& uri, const std::string& text)
{
    auto it = documents.find(uri);
    if (it != documents.end())
    {
        it->second->text = text;
        analyzeDocument(*it->second);
    }
}

void LspServer::closeDocument(const std::string& uri)
{
    documents.erase(uri);
}

// ============ LSP 方法 ============

llvm::json::Object LspServer::initialize(const llvm::json::Value& params)
{
    llvm::json::Object capabilities;
    capabilities["textDocumentSync"] = 1; // 全量同步
    capabilities["definitionProvider"] = true;
    capabilities["documentSymbolProvider"] = true;
    capabilities["renameProvider"] = true;
    capabilities["hoverProvider"] = true;
    capabilities["completionProvider"] = llvm::json::Object{
        {"triggerCharacters", llvm::json::Array{"."}},
    };
    capabilities["semanticTokensProvider"] = llvm::json::Object{
        {"legend", llvm::json::Object{
            {"tokenTypes", llvm::json::Array{"keyword", "type", "namespace", "function",
                                              "parameter", "variable", "property",
                                              "number", "string", "operator", "comment",
                                              "constant", "method"}},
            {"tokenModifiers", llvm::json::Array{"declaration", "readonly", "static"}},
        }},
        {"full", true},
    };

    llvm::json::Object result;
    result["capabilities"] = llvm::json::Value(std::move(capabilities));
    result["serverInfo"] = llvm::json::Object{{"name", "plang-lsp"}, {"version", "0.1.0"}};
    return result;
}

std::string LspServer::findSymbolNameAt(const DocumentState& doc, int line, int character)
{
    if (line < 0) return "";

    // 按行查找
    std::string lineText;
    std::istringstream stream(doc.text);
    std::string cur;
    for (int i = 0; i <= line; ++i)
    {
        if (!std::getline(stream, cur)) return "";
        lineText = cur;
    }

    if (character < 0 || character >= (int)lineText.size()) return "";

    // 从 character 位置扩展标识符
    int start = character;
    while (start > 0 && (std::isalnum(lineText[start - 1]) || lineText[start - 1] == '_')) start--;
    int end = character;
    while (end < (int)lineText.size() && (std::isalnum(lineText[end]) || lineText[end] == '_')) end++;

    if (start == end) return "";
    return lineText.substr(start, end - start);
}

llvm::json::Value LspServer::findDefinition(const std::string& uri, int line, int character)
{
    auto it = documents.find(uri);
    if (it == documents.end()) return nullptr;

    DocumentState& doc = *it->second;
    std::string name = findSymbolNameAt(doc, line, character);
    std::cerr << "[lsp] definition at " << line << ":" << character << " found name='" << name << "'\n";
    if (name.empty()) return nullptr;

    // 在符号表里找同名定义
    llvm::json::Array results;
    for (const auto& sym : doc.symbols)
    {
        if (sym.name == name)
        {
            llvm::json::Object loc;
            loc["uri"] = sym.uri;
            loc["range"] = llvm::json::Object{
                {"start", llvm::json::Object{{"line", sym.range.start.line}, {"character", sym.range.start.character}}},
                {"end", llvm::json::Object{{"line", sym.range.end.line}, {"character", sym.range.end.character}}},
            };
            results.push_back(llvm::json::Value(std::move(loc)));
        }
    }
    if (results.empty()) return nullptr;
    return llvm::json::Value(std::move(results));
}

llvm::json::Value LspServer::getHover(const std::string& uri, int line, int character)
{
    auto it = documents.find(uri);
    if (it == documents.end()) return nullptr;

    DocumentState& doc = *it->second;
    std::string name = findSymbolNameAt(doc, line, character);
    if (name.empty()) return nullptr;

    for (const auto& sym : doc.symbols)
    {
        if (sym.name == name)
        {
            std::string value = "**" + sym.name + "**";
            if (!sym.typeName.empty()) value += "  `" + sym.typeName + "`";
            value += "  (" + sym.kind + ")";
            return llvm::json::Object{
                {"contents", llvm::json::Object{{"kind", "markdown"}, {"value", value}}}
            };
        }
    }
    return nullptr;
}

llvm::json::Value LspServer::getCompletion(const std::string& uri, int line, int character)
{
    auto it = documents.find(uri);
    if (it == documents.end()) return nullptr;
    DocumentState& doc = *it->second;

    // 当前行与前缀
    std::string lineText;
    std::istringstream stream(doc.text);
    std::string cur;
    for (int i = 0; i <= line; ++i)
    {
        if (!std::getline(stream, cur)) break;
        lineText = cur;
    }
    int start = character;
    while (start > 0 && (std::isalnum(lineText[start - 1]) || lineText[start - 1] == '_')) start--;
    std::string prefix = lineText.substr(start, character - start);

    llvm::json::Array items;
    // 符号补全（本文件 + 合并的标准库）
    for (const auto& sym : doc.symbols)
    {
        if (prefix.empty() || sym.name.rfind(prefix, 0) == 0)
        {
            int lspKind = 6; // function
            if (sym.kind == "variable" || sym.kind == "parameter") lspKind = 6;
            else if (sym.kind == "type" || sym.kind == "struct") lspKind = 7;
            items.push_back(llvm::json::Object{
                {"label", sym.name},
                {"kind", lspKind},
                {"detail", sym.kind},
            });
        }
    }
    // 关键字补全
    static const std::vector<std::string> keywords = {
        "func", "var", "val", "if", "else", "while", "for", "return",
        "struct", "using", "import", "package", "pub", "switch", "case",
        "goto", "label", "thread", "io", "mem", "atomic", "null", "as"
    };
    for (const auto& kw : keywords)
    {
        if (prefix.empty() || kw.rfind(prefix, 0) == 0)
        {
            items.push_back(llvm::json::Object{
                {"label", kw},
                {"kind", 14}, // keyword
                {"detail", "keyword"},
            });
        }
    }
    return llvm::json::Object{
        {"isIncomplete", false},
        {"items", llvm::json::Value(std::move(items))},
    };
}

// 重命名：收集文档中该标识符的所有出现（跳过注释与字符串），生成 WorkspaceEdit
llvm::json::Value LspServer::getRename(const std::string& uri, int line, int character, const std::string& newName)
{
    auto it = documents.find(uri);
    if (it == documents.end()) return nullptr;

    DocumentState& doc = *it->second;
    std::string name = findSymbolNameAt(doc, line, character);
    if (name.empty() || newName.empty()) return nullptr;

    llvm::json::Array edits;
    std::istringstream stream(doc.text);
    std::string cur;
    for (int ln = 0; std::getline(stream, cur); ++ln)
    {
        bool inStr = false;
        for (int i = 0; i < (int)cur.size(); ++i)
        {
            char c = cur[i];
            if (inStr)
            {
                if (c == '\\') ++i;   // 转义：跳过下一字符
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"')
            {
                inStr = true;
                continue;
            }
            if (c == '/' && i + 1 < (int)cur.size() && cur[i + 1] == '/') break; // 行注释
            if (std::isalnum(c) || c == '_')
            {
                int start = i;
                while (i < (int)cur.size() && (std::isalnum(cur[i]) || cur[i] == '_')) ++i;
                if (cur.substr(start, i - start) == name)
                {
                    llvm::json::Object edit;
                    edit["range"] = llvm::json::Object{
                        {"start", llvm::json::Object{{"line", ln}, {"character", start}}},
                        {"end", llvm::json::Object{{"line", ln}, {"character", i}}},
                    };
                    edit["newText"] = newName;
                    edits.push_back(llvm::json::Value(std::move(edit)));
                }
                --i;
            }
        }
    }
    if (edits.empty()) return nullptr;
    llvm::json::Object changes;
    changes[uri] = llvm::json::Value(std::move(edits));
    return llvm::json::Value(llvm::json::Object{{"changes", llvm::json::Value(std::move(changes))}});
}

llvm::json::Value LspServer::getSymbols(const std::string& uri)
{
    auto it = documents.find(uri);
    if (it == documents.end()) return llvm::json::Array{};

    llvm::json::Array result;
    for (const auto& sym : it->second->symbols)
    {
        llvm::json::Object s;
        s["name"] = sym.name;
        s["kind"] = 12; // 简化
        s["range"] = llvm::json::Object{
            {"start", llvm::json::Object{{"line", sym.range.start.line}, {"character", sym.range.start.character}}},
            {"end", llvm::json::Object{{"line", sym.range.end.line}, {"character", sym.range.end.character}}},
        };
        s["selectionRange"] = llvm::json::Object{
            {"start", llvm::json::Object{{"line", sym.selectionRange.start.line}, {"character", sym.selectionRange.start.character}}},
            {"end", llvm::json::Object{{"line", sym.selectionRange.end.line}, {"character", sym.selectionRange.end.character}}},
        };
        result.push_back(llvm::json::Value(std::move(s)));
    }
    return llvm::json::Value(std::move(result));
}

llvm::json::Value LspServer::getSemanticTokens(const std::string& uri)
{
    auto it = documents.find(uri);
    if (it == documents.end())
    {
        llvm::json::Object r;
        r["data"] = llvm::json::Array{};
        return llvm::json::Value(std::move(r));
    }

    DocumentState& doc = *it->second;

    Lexer lexer(doc.text);
    auto tokens = lexer.scanTokens();

    // 语义 token 类型索引（与 legend 对应）
    // 0=keyword 1=type 2=namespace 3=function 4=parameter 5=variable
    // 6=property 7=number 8=string 9=operator 10=comment 11=constant 12=method
    auto tokenTypeOf = [this, &doc](TokenType t, const std::string& text) -> int {
        switch (t)
        {
            // 关键字
            case TokenType::PACKAGE: case TokenType::IMPORT:
            case TokenType::VAR: case TokenType::VAL: case TokenType::MOVE:
            case TokenType::FUNC: case TokenType::IMPL: case TokenType::RETURN:
            case TokenType::USING: case TokenType::STRUCT: case TokenType::ABSTRACT:
            case TokenType::PUB: case TokenType::PRT: case TokenType::PRI:
            case TokenType::THIS: case TokenType::THIS_TYPE: case TokenType::TYPE:
            case TokenType::AS: case TokenType::IF: case TokenType::ELSE:
            case TokenType::WHILE: case TokenType::FOR: case TokenType::DO:
                return 0;
            // 类型（内置类型名）
            case TokenType::INT: case TokenType::CHAR: case TokenType::STRING_TYPE:
            case TokenType::WCHAR: case TokenType::WSTRING: case TokenType::BOOL:
            case TokenType::I8: case TokenType::I16: case TokenType::I32: case TokenType::I64:
            case TokenType::U8: case TokenType::U16: case TokenType::U32: case TokenType::U64:
            case TokenType::UINT: case TokenType::F32: case TokenType::F64:
                return 1;
            case TokenType::NUMBER:
                return 7;
            case TokenType::STRING: case TokenType::CHAR_LIT:
                return 8;
            case TokenType::TRUE: case TokenType::FALSE:
                return 11; // 布尔常量
            case TokenType::IDENT:
            {
                // 用符号表 kind 细分
                for (const auto& sym : doc.symbols)
                {
                    if (sym.name != text) continue;
                    if (sym.kind == "function") return 3;
                    if (sym.kind == "parameter") return 4;
                    if (sym.kind == "constant") return 11;
                    if (sym.kind == "type") return 1;
                    return 5; // variable
                }
                return 5;
            }
            default:
                return 9; // 运算符/标点
        }
    };

    llvm::json::Array data;
    int prevLine = 0, prevChar = 0;

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const auto& tok = tokens[i];
        if (tok.type == TokenType::EOF_TOKEN || tok.type == TokenType::ERROR) continue;

        int line = tok.line - 1;       // 0 基
        int charStart = tok.column - 1;
        int len = (int)tok.text.size();

        // 判断是否为函数调用（IDENT 后跟 (）
        bool isFuncCall = (tok.type == TokenType::IDENT &&
                           i + 1 < tokens.size() && tokens[i + 1].type == TokenType::LPAREN);
        int typeIdx = tokenTypeOf(tok.type, tok.text);
        if (isFuncCall && (typeIdx == 5 || typeIdx == 11)) typeIdx = 12; // 调用 → method

        // 增量编码：相对上一 token
        data.push_back(line - prevLine);
        if (line == prevLine) {
            data.push_back(charStart - prevChar);
        } else {
            data.push_back(charStart);
        }
        data.push_back(len);
        data.push_back(typeIdx);
        data.push_back(0); // 无修饰符

        prevLine = line;
        prevChar = charStart;
    }

    llvm::json::Object result;
    result["data"] = llvm::json::Value(std::move(data));
    return llvm::json::Value(std::move(result));
}

llvm::json::Value LspServer::getDiagnostics(const std::string& uri)
{
    auto it = documents.find(uri);
    if (it == documents.end()) return llvm::json::Array{};

    llvm::json::Array result;
    for (const auto& err : it->second->errors)
    {
        llvm::json::Object diag;
        diag["range"] = llvm::json::Object{
            {"start", llvm::json::Object{{"line", err.line - 1}, {"character", err.column - 1}}},
            {"end", llvm::json::Object{{"line", err.line - 1}, {"character", err.column - 1}}},
        };
        diag["severity"] = 1;
        diag["message"] = err.message;
        result.push_back(llvm::json::Value(std::move(diag)));
    }
    return llvm::json::Value(std::move(result));
}

// ============ 请求分发 ============

llvm::json::Value LspServer::handleRequest(const std::string& method, const llvm::json::Value& params)
{
    if (method == "initialize")
    {
        return llvm::json::Value(initialize(params));
    }
    if (method == "textDocument/definition")
    {
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        auto* pos = obj->getObject("position");
        int line = (int)*pos->getInteger("line");
        int character = (int)*pos->getInteger("character");
        if (!uri) return nullptr;
        return findDefinition(uri->str(), line, character);
    }
    if (method == "textDocument/completion")
    {
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        auto* pos = obj->getObject("position");
        int line = (int)*pos->getInteger("line");
        int character = (int)*pos->getInteger("character");
        if (!uri) return nullptr;
        return getCompletion(uri->str(), line, character);
    }
    if (method == "textDocument/hover")
    {
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        auto* pos = obj->getObject("position");
        int line = (int)*pos->getInteger("line");
        int character = (int)*pos->getInteger("character");
        if (!uri) return nullptr;
        return getHover(uri->str(), line, character);
    }
    if (method == "textDocument/rename")
    {
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        auto* pos = obj->getObject("position");
        int line = (int)*pos->getInteger("line");
        int character = (int)*pos->getInteger("character");
        auto newName = obj->getString("newName");
        if (!uri || !newName) return nullptr;
        return getRename(uri->str(), line, character, newName->str());
    }
    if (method == "textDocument/documentSymbol")
    {
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        if (!uri) return nullptr;
        return getSymbols(uri->str());
    }
    if (method == "textDocument/semanticTokens/full")
    {
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        if (!uri) return nullptr;
        return getSemanticTokens(uri->str());
    }
    if (method == "textDocument/diagnostic")
    {
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        if (!uri) return nullptr;
        return getDiagnostics(uri->str());
    }
    if (method == "shutdown")
    {
        return nullptr;
    }
    return makeError(-32601, "method not found: " + method);
}

void LspServer::handleNotification(const std::string& method, const llvm::json::Value& params)
{
    if (method == "initialized") return;
    if (method == "exit") std::exit(0);

    if (method == "textDocument/didOpen")
    {
        std::cerr << "[lsp] didOpen\n";
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        auto text = td->getString("text");
        if (uri && text) openDocument(uri->str(), text->str());
    }
    else if (method == "textDocument/didChange")
    {
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        auto* changes = obj->getArray("contentChanges");
        if (uri && changes && !changes->empty())
        {
            auto* change = (*changes)[0].getAsObject();
            auto text = change->getString("text");
            if (text) updateDocument(uri->str(), text->str());
        }
    }
    else if (method == "textDocument/didClose")
    {
        auto* obj = params.getAsObject();
        auto* td = obj->getObject("textDocument");
        auto uri = td->getString("uri");
        if (uri) closeDocument(uri->str());
    }
}

// ============ 主循环 ============

void LspServer::run()
{
    while (true)
    {
        std::string body = readMessage();
        if (body.empty()) break;

        auto json = llvm::json::parse(body);
        if (!json) { std::cerr << "[lsp] json parse failed: " << body << "\n"; continue; }
        auto obj = json->getAsObject();
        if (!obj) { std::cerr << "[lsp] not an object\n"; continue; }

        std::string method;
        if (auto m = obj->getString("method")) method = m->str();
        auto id = obj->getInteger("id");

        if (id)
        {
            llvm::json::Value params = (obj->get("params") != nullptr)
                ? *obj->get("params") : llvm::json::Value(nullptr);
            auto result = handleRequest(method, params);
            sendResponse(*id, result);
        }
        else
        {
            llvm::json::Value params = (obj->get("params") != nullptr)
                ? *obj->get("params") : llvm::json::Value(nullptr);
            handleNotification(method, params);
        }
    }
}
