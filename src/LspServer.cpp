#include "../include/LspServer.h"
#include "../include/Lexer.h"
#include "../include/Parser.h"
#include "../include/Sema.h"
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
        return;
    }

    Sema sema;
    if (!sema.analyze(doc.program))
    {
        doc.errors = sema.getErrors();
    }
    doc.warnings = sema.getWarnings();
    collectSymbols(doc, doc.program.get());
    doc.parsed = true;
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
    capabilities["semanticTokensProvider"] = llvm::json::Object{
        {"legend", llvm::json::Object{
            {"tokenTypes", llvm::json::Array{"variable", "function", "type"}},
            {"tokenModifiers", llvm::json::Array{}},
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
    // 简化：返回空 token 流（后续用 Lexer/Sema 完善）
    llvm::json::Object result;
    result["data"] = llvm::json::Array{};
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
