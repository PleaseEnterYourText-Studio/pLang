#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "AST.h"

enum class SymbolKind
{
    VARIABLE,
    FUNCTION,
    STRUCT,
    TYPE,
    PARAMETER
};

enum class SymbolMutability
{
    VAR,    // 可变
    VAL,    // 只读
    MOVED   // 已移动（不可再使用）
};

struct Symbol
{
    std::string name;
    SymbolKind kind;
    SymbolMutability mutability;
    std::string typeName;       // 类型名（如 int, string, 或结构体名）
    int line;
    int column;
    std::string returnType;         // 函数返回类型
    std::vector<std::string> paramTypes;  // 函数参数类型
    std::string packageName;        // 所属包（如 "std.thread"）
    bool isPub;                     // 跨包可见
    bool isExtern;                  // extern FFI 声明

    Symbol(const std::string& name, SymbolKind kind, SymbolMutability mutability,
           const std::string& typeName, int line = 0, int column = 0)
        : name(name), kind(kind), mutability(mutability), typeName(typeName),
          line(line), column(column), isPub(false), isExtern(false) {}
};

// 单个作用域
struct Scope
{
    std::unordered_map<std::string, std::shared_ptr<Symbol>> symbols;
    std::shared_ptr<Scope> parent;

    explicit Scope(std::shared_ptr<Scope> parent = nullptr) : parent(parent) {}
};

// 嵌套作用域符号表
class SymbolTable
{
private:
    std::shared_ptr<Scope> current;

public:
    SymbolTable();

    void pushScope();
    void popScope();

    bool declare(const std::string& name, std::shared_ptr<Symbol> symbol);
    std::shared_ptr<Symbol> lookup(const std::string& name) const;
    std::shared_ptr<Symbol> lookupLocal(const std::string& name) const;
};

#endif
