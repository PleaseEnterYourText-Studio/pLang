#ifndef LSP_SERVER_H
#define LSP_SERVER_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "llvm/Support/JSON.h"
#include "AST.h"
#include "SymbolTable.h"

// LSP 位置（0 基）
struct LspPosition {
    int line;
    int character;
};

// LSP 范围
struct LspRange {
    LspPosition start;
    LspPosition end;
};

// 符号信息（跳转目标）
struct SymbolInfo {
    std::string name;
    std::string uri;
    LspRange range;
};

// 文档状态
struct DocumentState {
    std::string uri;
    std::string text;
    std::unique_ptr<ProgramNode> program;
    std::vector<SemaError> errors;
    bool parsed;

    DocumentState() : parsed(false) {}
};

// LSP 服务器：stdio 上运行 JSON-RPC
class LspServer
{
private:
    std::unordered_map<std::string, std::shared_ptr<DocumentState>> documents;
    std::string rootUri;

    // JSON-RPC
    llvm::json::Value handleRequest(const std::string& method, const llvm::json::Value& params);
    void handleNotification(const std::string& method, const llvm::json::Value& params);

    // 文档
    void openDocument(const std::string& uri, const std::string& text);
    void updateDocument(const std::string& uri, const std::string& text);
    void closeDocument(const std::string& uri);
    void analyzeDocument(DocumentState& doc);

    // LSP 方法
    llvm::json::Object initialize(const llvm::json::Value& params);
    llvm::json::Value findDefinition(const std::string& uri, int line, int character);
    llvm::json::Value getSemanticTokens(const std::string& uri);
    llvm::json::Value getSymbols(const std::string& uri);
    llvm::json::Value getDiagnostics(const std::string& uri);

    // 辅助
    LspPosition offsetToPosition(const DocumentState& doc, int offset) const;
    std::shared_ptr<Symbol> findSymbolAt(const DocumentState& doc, int line, int character);
    std::string positionToURI(const std::string& uri);

public:
    void run();
};

#endif
