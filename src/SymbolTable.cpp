#include "../include/SymbolTable.h"

SymbolTable::SymbolTable()
{
    current = std::make_shared<Scope>(nullptr);
}

void SymbolTable::pushScope()
{
    current = std::make_shared<Scope>(current);
}

void SymbolTable::popScope()
{
    if (current->parent)
    {
        current = current->parent;
    }
}

bool SymbolTable::declare(const std::string& name, std::shared_ptr<Symbol> symbol)
{
    if (current->symbols.find(name) != current->symbols.end())
    {
        return false; // 当前作用域已存在同名符号
    }
    current->symbols[name] = std::move(symbol);
    return true;
}

std::shared_ptr<Symbol> SymbolTable::lookup(const std::string& name) const
{
    auto scope = current;
    while (scope)
    {
        auto it = scope->symbols.find(name);
        if (it != scope->symbols.end())
        {
            return it->second;
        }
        scope = scope->parent;
    }
    return nullptr;
}

std::shared_ptr<Symbol> SymbolTable::lookupLocal(const std::string& name) const
{
    auto it = current->symbols.find(name);
    if (it != current->symbols.end())
    {
        return it->second;
    }
    return nullptr;
}
