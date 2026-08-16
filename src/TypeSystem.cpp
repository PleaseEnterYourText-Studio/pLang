#include "../include/TypeSystem.h"

// 整数排序表（小 → 大，用于拓宽判断）
static const std::vector<std::string> intRank = {
    "i8", "u8", "char", "i16", "u16", "int", "uint", "i32", "u32", "i64", "u64"
};

bool TypeInfo::isNumeric() const
{
    if (kind != PRIMITIVE) return false;
    for (const auto& t : intRank)
    {
        if (name == t) return true;
    }
    return name == "f32" || name == "f64";
}

bool TypeInfo::isBuiltin() const
{
    if (isNumeric()) return true;
    return name == "char" || name == "string" || name == "bool" ||
           name == "pointer" || name == "array" || name == "ptr" ||
           name == "func" || kind == POINTER || kind == ARRAY || kind == FUNC;
}

std::string TypeInfo::toString() const
{
    if (kind == POINTER)
    {
        return "pointer";
    }
    if (kind == ARRAY)
    {
        return "array";
    }
    if (kind == FUNC)
    {
        return "func";
    }
    return name.empty() ? "unknown" : name;
}

std::shared_ptr<TypeInfo> TypeSystem::fromTypeNode(TypeNode* t)
{
    auto info = std::make_shared<TypeInfo>();
    if (!t) return info;
    switch (t->baseType)
    {
        case ASTNodeType::TYPE_POINTER:
            info->kind = TypeInfo::POINTER;
            info->name = "pointer";
            info->elem = fromTypeNode(t->inner.get());
            return info;
        case ASTNodeType::TYPE_ARRAY:
            info->kind = TypeInfo::ARRAY;
            info->name = "array";
            info->arraySize = t->arraySize;
            info->elem = fromTypeNode(t->inner.get());
            return info;
        default:
            info->kind = TypeInfo::PRIMITIVE;
            info->name = t->name.empty() ? "unknown" : t->name;
            return info;
    }
}

std::shared_ptr<TypeInfo> TypeSystem::fromName(const std::string& name)
{
    auto info = std::make_shared<TypeInfo>();
    if (name == "pointer" || name == "ptr")
    {
        info->kind = TypeInfo::POINTER;
        info->name = "pointer";
        return info;
    }
    if (name == "array")
    {
        info->kind = TypeInfo::ARRAY;
        info->name = "array";
        return info;
    }
    if (name == "func")
    {
        info->kind = TypeInfo::FUNC;
        info->name = "func";
        return info;
    }
    info->kind = TypeInfo::PRIMITIVE;
    info->name = name.empty() ? "unknown" : name;
    return info;
}

bool TypeSystem::equal(const std::shared_ptr<TypeInfo>& a, const std::shared_ptr<TypeInfo>& b)
{
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    // int/i32、uint/u32 别名
    if ((a->name == "int" && b->name == "i32") || (a->name == "i32" && b->name == "int")) return true;
    if ((a->name == "uint" && b->name == "u32") || (a->name == "u32" && b->name == "uint")) return true;
    // ptr/pointer 别名
    if ((a->name == "ptr" && b->name == "pointer") || (a->name == "pointer" && b->name == "ptr")) return true;

    if (a->kind == TypeInfo::POINTER)
    {
        return a->name == b->name;   // 指针一律兼容，不看指向
    }
    if (a->kind == TypeInfo::ARRAY)
    {
        return a->arraySize == b->arraySize && equal(a->elem, b->elem);
    }
    return a->name == b->name;
}

bool TypeSystem::compatible(const std::shared_ptr<TypeInfo>& from, const std::shared_ptr<TypeInfo>& to)
{
    if (!from || !to) return false;
    if (equal(from, to)) return true;

    // 字符串 → 指针衰减
    if (from->name == "string" && to->kind == TypeInfo::POINTER) return true;

    // func（函数指针）与 pointer 兼容
    if ((from->kind == TypeInfo::FUNC && to->kind == TypeInfo::POINTER) ||
        (from->kind == TypeInfo::POINTER && to->kind == TypeInfo::FUNC)) return true;

    // 整数拓宽
    if (widening(from, to)) return true;

    // 整数 → 浮点
    if (to->name == "f32" || to->name == "f64")
    {
        return from->isNumeric() && from->name != "f32" && from->name != "f64";
    }

    return false;
}

bool TypeSystem::widening(const std::shared_ptr<TypeInfo>& from, const std::shared_ptr<TypeInfo>& to)
{
    if (!from || !to || from->kind != TypeInfo::PRIMITIVE || to->kind != TypeInfo::PRIMITIVE) return false;
    size_t rf = intRank.size(), rt = intRank.size();
    for (size_t i = 0; i < intRank.size(); ++i)
    {
        if (intRank[i] == from->name) rf = i;
        if (intRank[i] == to->name) rt = i;
    }
    return rf < intRank.size() && rt < intRank.size() && rf < rt;
}
