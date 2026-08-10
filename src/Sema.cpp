#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "../include/Sema.h"

void Sema::error(int line, int column, const std::string& message)
{
    errors.emplace_back(line, column, message);
}

void Sema::warn(int line, int column, const std::string& message)
{
    warnings.emplace_back(line, column, message);
}

bool Sema::analyze(std::unique_ptr<ProgramNode>& program)
{
    errors.clear();
    warnings.clear();
    visitProgram(program.get());
    return errors.empty();
}

// 顶层

void Sema::visitProgram(ProgramNode* node)
{
    if (node->packageName.empty())
    {
        error(node->line, node->column, "missing package declaration");
    }

    // 第一阶段：收集所有顶层声明（函数/结构体），允许前向引用
    for (auto& decl : node->decls)
    {
        if (decl->type == ASTNodeType::FUNCTION_DECL)
        {
            auto* funcNode = dynamic_cast<FunctionDeclNode*>(decl.get());
            auto sym = std::make_shared<Symbol>(funcNode->name, SymbolKind::FUNCTION,
                                                SymbolMutability::VAL, "fn",
                                                funcNode->line, funcNode->column);
            sym->returnType = funcNode->returnType ? typeNodeToName(funcNode->returnType.get()) : "";
            for (auto& p : funcNode->params)
            {
                sym->paramTypes.push_back(p->type ? typeNodeToName(p->type.get()) : "");
            }
            if (!symbols.declare(funcNode->name, sym))
            {
                error(funcNode->line, funcNode->column, "duplicate function '" + funcNode->name + "'");
            }
        }
        else if (decl->type == ASTNodeType::STRUCT_DECL)
        {
            auto* structNode = dynamic_cast<StructDeclNode*>(decl.get());
            auto sym = std::make_shared<Symbol>(structNode->name, SymbolKind::STRUCT,
                                                SymbolMutability::VAL, "type",
                                                structNode->line, structNode->column);
            if (!symbols.declare(structNode->name, sym))
            {
                error(structNode->line, structNode->column, "duplicate type '" + structNode->name + "'");
            }
        }
        else if (decl->type == ASTNodeType::USING_DECL)
        {
            // using Point = struct {...} —— 结构体藏在 UsingDeclNode 里
            auto* usingNode = dynamic_cast<UsingDeclNode*>(decl.get());
            if (usingNode->aliased && usingNode->aliased->type == ASTNodeType::STRUCT_DECL)
            {
                auto* structNode = dynamic_cast<StructDeclNode*>(usingNode->aliased.get());
                auto sym = std::make_shared<Symbol>(structNode->name, SymbolKind::STRUCT,
                                                    SymbolMutability::VAL, "type",
                                                    structNode->line, structNode->column);
                if (!symbols.declare(structNode->name, sym))
                {
                    error(structNode->line, structNode->column, "duplicate type '" + structNode->name + "'");
                }
            }
        }
    }

    // 第二阶段：检查函数体
    symbols.pushScope();
    for (auto& decl : node->decls)
    {
        visitDecl(decl.get());
    }
    symbols.popScope();
}

void Sema::visitDecl(ASTNode* node)
{
    switch (node->type)
    {
        case ASTNodeType::FUNCTION_DECL:
            visitFunctionDecl(dynamic_cast<FunctionDeclNode*>(node));
            break;
        case ASTNodeType::STRUCT_DECL:
            visitStructDecl(dynamic_cast<StructDeclNode*>(node));
            break;
        case ASTNodeType::USING_DECL:
        {
            auto* usingNode = dynamic_cast<UsingDeclNode*>(node);
            if (usingNode->aliased && usingNode->aliased->type == ASTNodeType::STRUCT_DECL)
            {
                visitStructDecl(dynamic_cast<StructDeclNode*>(usingNode->aliased.get()));
            }
            break;
        }
        case ASTNodeType::IMPL_DECL:
            visitImplDecl(dynamic_cast<ImplDeclNode*>(node));
            break;
        default:
            break;
    }
}

// 函数

void Sema::visitFunctionDecl(FunctionDeclNode* node)
{
    symbols.pushScope();
    currentReturnType = node->returnType ? typeNodeToName(node->returnType.get()) : "";
    currentFunctionName = node->name;

    // 参数
    for (auto& param : node->params)
    {
        if (param->type)
        {
            auto sym = std::make_shared<Symbol>(param->name, SymbolKind::PARAMETER,
                                                param->isVar ? SymbolMutability::VAR : SymbolMutability::VAL,
                                                typeNodeToName(param->type.get()),
                                                param->line, param->column);
            if (!symbols.declare(param->name, sym))
            {
                error(param->line, param->column, "duplicate parameter '" + param->name + "'");
            }
        }
        else
        {
            error(param->line, param->column, "parameter '" + param->name + "' is missing a type");
        }
    }

    if (node->body)
    {
        visitBlock(node->body.get());
    }

    symbols.popScope();
    currentReturnType.clear();
    currentFunctionName.clear();
}

// 结构体

void Sema::visitStructDecl(StructDeclNode* node)
{
    // 检查继承重名（基类重复出现 = 菱形继承，报错）
    std::vector<std::string> seen;
    for (auto& base : node->bases)
    {
        for (auto& prev : seen)
        {
            if (prev == base)
            {
                error(node->line, node->column, "diamond inheritance: duplicate base '" + base + "'");
            }
        }
        seen.push_back(base);

        auto sym = symbols.lookup(base);
        if (!sym)
        {
            error(node->line, node->column, "unknown base type '" + base + "'");
        }
    }

    // 检查成员（仅做类型引用检查，不深入）
    symbols.pushScope();
    for (auto& member : node->members)
    {
        if (member->type == ASTNodeType::VARIABLE_DECL)
        {
            auto* varNode = dynamic_cast<VariableDeclNode*>(member.get());
            if (varNode->type)
            {
                std::string typeName = typeNodeToName(varNode->type.get());
                if (typeName == "unknown" || typeName == "unknown type")
                {
                    error(varNode->line, varNode->column, "unknown member type");
                }
                else if (varNode->type->baseType == ASTNodeType::TYPE_PRIMITIVE && !typeName.empty())
                {
                    if (!isBuiltinType(typeName))
                    {
                    auto typeSym = symbols.lookup(typeName);
                    if (!typeSym || typeSym->kind != SymbolKind::STRUCT)
                    {
                        error(varNode->type->line, varNode->type->column, "unknown type '" + typeName + "'");
                    }
                    }
                }
            }
        }
    }
    symbols.popScope();
}

void Sema::visitImplDecl(ImplDeclNode* node)
{
    // 检查 impl 目标是否存在
    std::string target = node->target.substr(0, node->target.find('.'));
    auto sym = symbols.lookup(target);
    if (!sym)
    {
        error(node->line, node->column, "impl target '" + target + "' is not defined");
    }
}

// 语句

void Sema::visitStmt(ASTNode* node)
{
    if (!node) return;
    switch (node->type)
    {
        case ASTNodeType::BLOCK_STMT:
            visitBlock(dynamic_cast<BlockStmtNode*>(node));
            break;
        case ASTNodeType::VARIABLE_DECL:
            visitVarDecl(dynamic_cast<VariableDeclNode*>(node));
            break;
        case ASTNodeType::IF_STMT:
            visitIf(dynamic_cast<IfStmtNode*>(node));
            break;
        case ASTNodeType::WHILE_STMT:
            visitWhile(dynamic_cast<WhileStmtNode*>(node));
            break;
        case ASTNodeType::FOR_STMT:
            visitFor(dynamic_cast<ForStmtNode*>(node));
            break;
        case ASTNodeType::RETURN_STMT:
            visitReturn(dynamic_cast<ReturnStmtNode*>(node));
            break;
        case ASTNodeType::EXPRESSION_STMT:
            visitExprStmt(dynamic_cast<ExpressionStmtNode*>(node));
            break;
        case ASTNodeType::ASSIGNMENT_STMT:
            visitExpr(dynamic_cast<AssignmentNode*>(node));
            break;
        default:
            break;
    }
}

void Sema::visitBlock(BlockStmtNode* node)
{
    symbols.pushScope();
    for (auto& stmt : node->statements)
    {
        visitStmt(stmt.get());
    }
    symbols.popScope();
}

void Sema::visitVarDecl(VariableDeclNode* node)
{
    std::string declaredType;
    if (node->type)
    {
        declaredType = typeNodeToName(node->type.get());

        // 检查用户自定义类型（IDENT）是否已定义
        if (node->type->baseType == ASTNodeType::TYPE_PRIMITIVE && !declaredType.empty())
        {
            if (!isBuiltinType(declaredType))
            {
                auto typeSym = symbols.lookup(declaredType);
                if (!typeSym || typeSym->kind != SymbolKind::STRUCT)
                {
                    error(node->type->line, node->type->column, "unknown type '" + declaredType + "'");
                }
            }
        }
    }

    std::string initType;
    if (node->initializer)
    {
        initType = visitExpr(node->initializer.get());
    }

    if (node->isMoved)
    {
        // move 声明：初始值必须是一个变量引用
        if (node->initializer && node->initializer->type != ASTNodeType::VARIABLE_REF)
        {
            error(node->line, node->column, "move requires a variable reference on the right side");
        }
        else if (node->initializer)
        {
            auto* ref = dynamic_cast<VariableRefNode*>(node->initializer.get());
            auto sym = symbols.lookup(ref->name);
            if (sym)
            {
                sym->mutability = SymbolMutability::MOVED; // 原变量失效
            }
        }
    }

    // 类型兼容
    if (!declaredType.empty() && !initType.empty())
    {
        if (!isCompatible(initType, declaredType))
        {
            error(node->line, node->column, "cannot initialize '" + node->name + "' of type '" +
                  declaredType + "' with value of type '" + initType + "'");
        }
    }

    SymbolMutability mut = node->isVar ? SymbolMutability::VAR : SymbolMutability::VAL;
    auto sym = std::make_shared<Symbol>(node->name, SymbolKind::VARIABLE, mut,
                                        declaredType.empty() ? initType : declaredType,
                                        node->line, node->column);
    if (!symbols.declare(node->name, sym))
    {
        error(node->line, node->column, "duplicate variable '" + node->name + "'");
    }
}

void Sema::visitIf(IfStmtNode* node)
{
    if (node->condition)
    {
        visitExpr(node->condition.get());
    }
    if (node->thenBranch) visitStmt(node->thenBranch.get());
    if (node->elseBranch) visitStmt(node->elseBranch.get());
}

void Sema::visitWhile(WhileStmtNode* node)
{
    if (node->condition)
    {
        visitExpr(node->condition.get());
    }
    if (node->body) visitStmt(node->body.get());
}

void Sema::visitFor(ForStmtNode* node)
{
    symbols.pushScope();
    if (node->init) visitStmt(node->init.get());
    if (node->condition) visitExpr(node->condition.get());
    if (node->update) visitExpr(node->update.get());
    if (node->body) visitStmt(node->body.get());
    symbols.popScope();
}

void Sema::visitReturn(ReturnStmtNode* node)
{
    if (node->value)
    {
        std::string valueType = visitExpr(node->value.get());
        if (!currentReturnType.empty())
        {
            if (!isCompatible(valueType, currentReturnType))
            {
                error(node->line, node->column, "cannot return value of type '" + valueType +
                      "' from function returning '" + currentReturnType + "'");
            }
        }
    }
    else
    {
        if (!currentReturnType.empty())
        {
            error(node->line, node->column, "return missing value for function returning '" +
                  currentReturnType + "'");
        }
    }
}

void Sema::visitExprStmt(ExpressionStmtNode* node)
{
    if (node->expr)
    {
        visitExpr(node->expr.get());
    }
}

// 表达式

std::string Sema::visitExpr(ASTNode* node)
{
    if (!node) return "";
    switch (node->type)
    {
        case ASTNodeType::BINARY_OP: return visitBinary(dynamic_cast<BinaryOpNode*>(node));
        case ASTNodeType::UNARY_OP:
        {
            // 取地址 &a → pointer
            if (auto* addr = dynamic_cast<AddressOfNode*>(node))
            {
                visitExpr(addr->operand.get());
                return "pointer";
            }
            return visitUnary(dynamic_cast<UnaryOpNode*>(node));
        }
        case ASTNodeType::COMPARISON_OP: return visitComparison(dynamic_cast<ComparisonOpNode*>(node));
        case ASTNodeType::LOGICAL_OP: return visitLogical(dynamic_cast<LogicalOpNode*>(node));
        case ASTNodeType::FUNCTION_CALL: return visitCall(dynamic_cast<FunctionCallNode*>(node));
        case ASTNodeType::LITERAL_INT: return visitLiteralInt(dynamic_cast<LiteralIntNode*>(node));
        case ASTNodeType::LITERAL_FLOAT: return visitLiteralFloat(dynamic_cast<LiteralFloatNode*>(node));
        case ASTNodeType::LITERAL_STRING: return visitLiteralString(dynamic_cast<LiteralStringNode*>(node));
        case ASTNodeType::LITERAL_BOOL: return visitLiteralBool(dynamic_cast<LiteralBoolNode*>(node));
        case ASTNodeType::VARIABLE_REF: return visitVariableRef(dynamic_cast<VariableRefNode*>(node));
        case ASTNodeType::ASSIGNMENT_STMT: return visitAssignment(dynamic_cast<AssignmentNode*>(node));
        case ASTNodeType::CAST:
        {
            auto* castNode = dynamic_cast<CastNode*>(node);
            visitExpr(castNode->value.get());
            return castNode->targetType;
        }
        default: return "";
    }
}

std::string Sema::visitBinary(BinaryOpNode* node)
{
    std::string leftType = visitExpr(node->lift.get());
    std::string rightType = visitExpr(node->right.get());
    if (!leftType.empty() && !rightType.empty())
    {
        if (!isNumericType(leftType) || !isNumericType(rightType))
        {
            error(node->line, node->column, "arithmetic on non-numeric types '" + leftType + "' and '" + rightType + "'");
        }
    }
    return leftType.empty() ? rightType : leftType;
}

std::string Sema::visitUnary(UnaryOpNode* node)
{
    std::string operandType = visitExpr(node->operand.get());
    if (node->op == UnaryOpType::DEREF)
    {
        if (operandType != "pointer" && !operandType.empty())
        {
            error(node->line, node->column, "cannot dereference non-pointer type '" + operandType + "'");
        }
        return "int"; // 简化：解引用返回 int
    }
    return operandType;
}

std::string Sema::visitComparison(ComparisonOpNode* node)
{
    visitExpr(node->lift.get());
    visitExpr(node->right.get());
    return "bool";
}

std::string Sema::visitLogical(LogicalOpNode* node)
{
    std::string leftType = visitExpr(node->lift.get());
    std::string rightType = visitExpr(node->right.get());
    if (!leftType.empty() && leftType != "bool")
    {
        error(node->line, node->column, "logical && requires bool operands, got '" + leftType + "'");
    }
    if (!rightType.empty() && rightType != "bool")
    {
        error(node->line, node->column, "logical || requires bool operands, got '" + rightType + "'");
    }
    return "bool";
}

std::string Sema::visitCall(FunctionCallNode* node)
{
    // 成员方法调用 circle.area() —— 拆分根对象与方法名
    size_t dot = node->name.find('.');
    if (dot != std::string::npos)
    {
        std::string objName = node->name.substr(0, dot);
        std::string methodName = node->name.substr(dot + 1);

        auto objSym = symbols.lookup(objName);
        if (!objSym)
        {
            error(node->line, node->column, "call to method on undefined object '" + objName + "'");
            return "";
        }
        // 简化：方法返回值类型暂按结构体成员推导，这里返回 "int" 占位
        // 后续接入结构体成员表后完善
        for (auto& arg : node->arguments)
        {
            visitExpr(arg.get());
        }
        return "f64";
    }

    auto sym = symbols.lookup(node->name);
    if (!sym)
    {
        error(node->line, node->column, "call to undefined function '" + node->name + "'");
        return "";
    }
    if (sym->kind != SymbolKind::FUNCTION)
    {
        error(node->line, node->column, "'" + node->name + "' is not a function");
        return "";
    }

    // 参数数量检查
    if (!sym->paramTypes.empty() || !node->arguments.empty())
    {
        if (sym->paramTypes.size() != node->arguments.size())
        {
            error(node->line, node->column, "function '" + node->name + "' expects " +
                  std::to_string(sym->paramTypes.size()) + " argument(s), got " +
                  std::to_string(node->arguments.size()));
        }
    }

    for (size_t i = 0; i < node->arguments.size() && i < sym->paramTypes.size(); ++i)
    {
        std::string argType = visitExpr(node->arguments[i].get());
        if (!argType.empty() && !sym->paramTypes[i].empty())
        {
            if (!isCompatible(argType, sym->paramTypes[i]))
            {
                error(node->line, node->column, "argument " + std::to_string(i + 1) + " of '" +
                      node->name + "' expects '" + sym->paramTypes[i] + "', got '" + argType + "'");
            }
        }
    }
    return sym->returnType;
}

std::string Sema::visitLiteralInt(LiteralIntNode* node)
{
    if (node->suffix == "ll" || node->suffix == "LL") return "i64";
    return "int";
}
std::string Sema::visitLiteralFloat(LiteralFloatNode* node) { return node->suffix == "f" ? "f32" : "f64"; }
std::string Sema::visitLiteralString(LiteralStringNode* node) { return node->isChar ? "char" : "string"; }
std::string Sema::visitLiteralBool(LiteralBoolNode* node) { return "bool"; }

std::string Sema::visitVariableRef(VariableRefNode* node)
{
    // 成员访问 s.a.foo —— 拆出最前面的名字
    std::string root = node->name.substr(0, node->name.find('.'));
    auto sym = symbols.lookup(root);
    if (!sym)
    {
        error(node->line, node->column, "undefined variable '" + root + "'");
        return "";
    }
    if (sym->mutability == SymbolMutability::MOVED)
    {
        error(node->line, node->column, "use of moved variable '" + root + "'");
        return sym->typeName; // 仍返回真实类型，避免级联误报
    }
    return sym->typeName;
}

std::string Sema::visitAssignment(AssignmentNode* node)
{
    std::string targetType = visitExpr(node->target.get());
    std::string valueType = visitExpr(node->value.get());

    // 检查 val 只读
    if (node->target->type == ASTNodeType::VARIABLE_REF)
    {
        auto* ref = dynamic_cast<VariableRefNode*>(node->target.get());
        auto sym = symbols.lookup(ref->name.substr(0, ref->name.find('.')));
        if (sym && sym->mutability == SymbolMutability::VAL)
        {
            error(node->line, node->column, "cannot assign to val variable '" + ref->name + "'");
        }
    }

    if (!targetType.empty() && !valueType.empty())
    {
        if (!isCompatible(valueType, targetType))
        {
            error(node->line, node->column, "cannot assign value of type '" + valueType +
                  "' to variable of type '" + targetType + "'");
        }
        else if (isWidening(valueType, targetType))
        {
            warn(node->line, node->column, "assignment from '" + valueType + "' to '" +
                 targetType + "' may lose precision");
        }
    }
    return targetType;
}

// 类型工具

std::string Sema::typeNodeToName(TypeNode* type)
{
    if (!type) return "";
    switch (type->baseType)
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
        case ASTNodeType::TYPE_STRING: return "string";
        case ASTNodeType::TYPE_CHAR: return "char";
        case ASTNodeType::TYPE_BOOL: return "bool";
        case ASTNodeType::TYPE_POINTER: return "pointer";
        case ASTNodeType::TYPE_ARRAY: return "array";
        case ASTNodeType::TYPE_PRIMITIVE: return type->name;
        default: return type->name.empty() ? "unknown" : type->name;
    }
}

bool Sema::isNumericType(const std::string& type) const
{
    return type == "int" || type == "i8" || type == "i16" || type == "i64" ||
           type == "uint" || type == "u8" || type == "u16" || type == "u64" ||
           type == "f32" || type == "f64";
}

bool Sema::isBuiltinType(const std::string& type) const
{
    return isNumericType(type) || type == "char" || type == "string" ||
           type == "bool" || type == "pointer" || type == "array" ||
           type == "wchar" || type == "wstring";
}

bool Sema::isCompatible(const std::string& from, const std::string& to) const
{
    if (from == to) return true;

    // int/uint 别名
    if ((from == "int" && to == "i32") || (from == "i32" && to == "int")) return true;
    if ((from == "uint" && to == "u32") || (from == "u32" && to == "uint")) return true;

    // 整数提升：小类型可安全赋给大类型
    static const std::vector<std::string> intRank = {
        "i8", "u8", "i16", "u16", "int", "uint", "i32", "u32", "i64", "u64"
    };
    size_t rf = intRank.size(), rt = intRank.size();
    for (size_t i = 0; i < intRank.size(); ++i)
    {
        if (intRank[i] == from) rf = i;
        if (intRank[i] == to) rt = i;
    }
    if (rf < intRank.size() && rt < intRank.size() && rf < rt)
    {
        return true; // 整数拓宽允许
    }

    // 整数 → 浮点
    if (to == "f32" || to == "f64")
    {
        if (rf < intRank.size()) return true;
    }

    return false;
}

bool Sema::isWidening(const std::string& from, const std::string& to) const
{
    // from 比 to 宽：窄化赋值，可能丢失精度
    if (from == "f64" && (to == "f32" || isNumericType(to) && to != "f64")) return true;
    if (from == "f32" && to != "f64" && isNumericType(to)) return true;
    if ((from == "i64" || from == "u64") && (to == "int" || to == "i32" || to == "uint" ||
         to == "u32" || to == "i8" || to == "u8")) return true;
    if ((from == "i32" || from == "u32") && (to == "i8" || to == "u8" || to == "i16" || to == "u16")) return true;
    return false;
}
