#ifndef LSP_SERVER_H
#define LSP_SERVER_H

#include <string>
#include <vector>
#include <memory>
#include <set>
#include <unordered_map>
#include "llvm/Support/JSON.h"
#include "AST.h"
#include "SymbolTable.h"
#include "Sema.h"

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
    LspRange selectionRange;
    std::string kind;   // function / variable / parameter / type / struct
    std::string typeName;   // 类型（悬停显示用）
    std::string packageName;  // 所属包（std.io 等；空=本文件）
};

// 文档状态
struct DocumentState {
    std::string uri;
    std::string text;
    std::unique_ptr<ProgramNode> program;
    std::vector<SemaError> errors;
    std::vector<SemaWarning> warnings;
    std::vector<SymbolInfo> symbols;
    std::set<std::string> importedModules;   // 已导入的包（如 std.io）
    bool parsed;

    DocumentState() : parsed(false) {}
};

// LSP 服务器：stdio 上运行 JSON-RPC
class LspServer
{
private:
    std::unordered_map<std::string, std::shared_ptr<DocumentState>> documents;
    std::string rootUri;
    bool clientSupportsSemanticTokens = false;

    // JSON-RPC
    llvm::json::Value handleRequest(const std::string& method, const llvm::json::Value& params);
    void handleNotification(const std::string& method, const llvm::json::Value& params);
    void sendResponse(int id, const llvm::json::Value& result);
    void sendNotification(const std::string& method, const llvm::json::Value& params);
    llvm::json::Value makeError(int code, const std::string& message);

    // 消息解析（Content-Length 帧）
    std::string readMessage();

    // 文档
    void openDocument(const std::string& uri, const std::string& text);
    void updateDocument(const std::string& uri, const std::string& text);
    void closeDocument(const std::string& uri);
    void analyzeDocument(DocumentState& doc);
    void collectSymbols(DocumentState& doc, ASTNode* node);
    void collectStdlibSymbols(DocumentState& doc, ASTNode* node,
                              const std::string& realUri);   // 标准库符号（带真实文件 uri，跳转用）

    // LSP 方法
    llvm::json::Object initialize(const llvm::json::Value& params);
    llvm::json::Value findDefinition(const std::string& uri, int line, int character);
    llvm::json::Value getSemanticTokens(const std::string& uri);
    llvm::json::Value getSymbols(const std::string& uri);
    llvm::json::Value getHover(const std::string& uri, int line, int character);
    llvm::json::Value getCompletion(const std::string& uri, int line, int character);
    llvm::json::Value getRename(const std::string& uri, int line, int character, const std::string& newName);
    llvm::json::Value getDiagnostics(const std::string& uri);

    // 辅助
    LspPosition offsetToPosition(const DocumentState& doc, int offset) const;
    std::string findSymbolNameAt(const DocumentState& doc, int line, int character);

public:
    void run();
};

#endif
