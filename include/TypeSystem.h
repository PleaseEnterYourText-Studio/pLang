#ifndef TYPESYSTEM_H
#define TYPESYSTEM_H

#include <string>
#include <memory>
#include <vector>
#include "AST.h"

// 类型对象（D1 类型系统重构基础层）：把字符串类型名提升为结构化对象，
// 统一等价/拓宽判断；CodeGenerator 侧迁移待后续
struct TypeInfo
{
    enum Kind
    {
        PRIMITIVE,  // 内建标量（int/f64/bool/char/string...）
        POINTER,    // var -> var: T / ptr
        ARRAY,      // T[N]
        STRUCT,     // 用户结构体（含泛型实例化名）
        FUNC,       // 函数指针
        UNKNOWN
    };

    Kind kind = UNKNOWN;
    std::string name;                       // 规范化名字（打印/查找用）
    std::shared_ptr<TypeInfo> elem;         // POINTER 指向 / ARRAY 元素
    int arraySize = 0;                      // ARRAY 长度
    std::vector<std::string> typeArgs;      // 泛型实参（Struct<int> 的 ["int"]）

    bool isNumeric() const;
    bool isBuiltin() const;
    std::string toString() const;           // 可打印（与 typeNodeToName 兼容）
};

// 类型系统入口：TypeNode → TypeInfo 对象
class TypeSystem
{
public:
    static std::shared_ptr<TypeInfo> fromTypeNode(TypeNode* t);
    static std::shared_ptr<TypeInfo> fromName(const std::string& name);  // 字符串名 → 对象（尽力解析）

    // 类型等价（含 int/i32、ptr/pointer 别名）
    static bool equal(const std::shared_ptr<TypeInfo>& a, const std::shared_ptr<TypeInfo>& b);
    // 兼容：from 可赋给 to（等宽 + 拓宽 + 字符串→指针衰减）
    static bool compatible(const std::shared_ptr<TypeInfo>& from, const std::shared_ptr<TypeInfo>& to);
    // 整数拓宽（小类型可安全赋给大类型）
    static bool widening(const std::shared_ptr<TypeInfo>& from, const std::shared_ptr<TypeInfo>& to);
};

#endif
