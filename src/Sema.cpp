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
    arrayElementTypes.clear();
    pointerElementTypes.clear();
    structRegistry.clear();
    genericTemplates.clear();
    currentProgram = program.get();
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

    // 收集导入的模块（如 import std.thread;）
    importedModules.clear();
    for (auto& imp : node->imports)
    {
        if (imp->type == ASTNodeType::IMPORT_STMT)
        {
            auto* importNode = dynamic_cast<ImportStmtNode*>(imp.get());
            importedModules.insert(importNode->path);
        }
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
            sym->packageName = funcNode->packageName;
            sym->isPub = funcNode->isPub;
            sym->isExtern = funcNode->isExtern;
            sym->isVariadic = funcNode->isVariadic;
            if (!symbols.declare(funcNode->name, sym))
            {
                error(funcNode->line, funcNode->column, "duplicate function '" + funcNode->name + "'");
            }
            // 包限定别名注册：<别名>.<函数名>（别名为包路径最后一段，如 thread.join）
            if (!funcNode->packageName.empty())
            {
                size_t lastDot = funcNode->packageName.rfind('.');
                std::string alias = (lastDot == std::string::npos)
                    ? funcNode->packageName
                    : funcNode->packageName.substr(lastDot + 1);
                if (!alias.empty() && alias != funcNode->name)
                {
                    symbols.declare(alias + "." + funcNode->name, sym);
                }
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
            // 登记结构体字段（成员访问 s.x 用）
            StructInfo info;
            for (auto& m : structNode->members)
            {
                if (m->type == ASTNodeType::VARIABLE_DECL)
                {
                    auto* field = dynamic_cast<VariableDeclNode*>(m.get());
                    if (field->type)
                    {
                        info.fields.emplace_back(field->name, typeNodeToName(field->type.get()));
                        // 数组成员元素类型（s.arr[i] 用）
                        if (field->type->baseType == ASTNodeType::TYPE_ARRAY && field->type->inner)
                        {
                            info.fieldElementTypes[field->name] = typeNodeToName(field->type->inner.get());
                        }
                    }
                }
            }
            if (!structNode->typeParams.empty())
            {
                genericTemplates[structNode->name] = structNode; // 泛型模板（不注册具体类型）
            }
            else
            {
                structRegistry[structNode->name] = std::move(info);
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
                // 登记结构体字段
                StructInfo info;
                for (auto& m : structNode->members)
                {
                    if (m->type == ASTNodeType::VARIABLE_DECL)
                    {
                        auto* field = dynamic_cast<VariableDeclNode*>(m.get());
                        if (field->type)
                        {
                            info.fields.emplace_back(field->name, typeNodeToName(field->type.get()));
                            // 数组成员元素类型（s.arr[i] 用）
                            if (field->type->baseType == ASTNodeType::TYPE_ARRAY && field->type->inner)
                            {
                                info.fieldElementTypes[field->name] = typeNodeToName(field->type->inner.get());
                            }
                        }
                    }
                }
                // 登记结构体方法签名（obj.method() 解析用）
                for (auto& m : structNode->members)
                {
                    if (m->type == ASTNodeType::FUNCTION_DECL)
                    {
                        auto* fn = dynamic_cast<FunctionDeclNode*>(m.get());
                        std::vector<std::string> pt;
                        for (auto& p : fn->params)
                            pt.push_back(p->type ? typeNodeToName(p->type.get()) : "");
                        info.methods[fn->name] = { fn->returnType ? typeNodeToName(fn->returnType.get()) : "",
                                                   std::move(pt) };
                        if (fn->name == ".construction") info.hasConstruction = true;
                        if (fn->name == ".destroy") info.hasDestruction = true;
                    }
                }
                if (!structNode->typeParams.empty())
                {
                    genericTemplates[structNode->name] = structNode; // 泛型模板
                }
                else
                {
                    structRegistry[structNode->name] = std::move(info);
                }
            }
        }
        else if (decl->type == ASTNodeType::EXTERN_VAR_DECL)
        {
            // extern 全局数据：extern var stdin : ptr;
            auto* ev = dynamic_cast<ExternVarDeclNode*>(decl.get());
            auto sym = std::make_shared<Symbol>(ev->name, SymbolKind::VARIABLE,
                                                ev->isVar ? SymbolMutability::VAR : SymbolMutability::VAL,
                                                typeNodeToName(ev->type.get()),
                                                ev->line, ev->column);
            if (!symbols.declare(ev->name, sym))
            {
                error(ev->line, ev->column, "duplicate extern variable '" + ev->name + "'");
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
        case ASTNodeType::EXTERN_VAR_DECL:
            break; // 已在第一阶段注册
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
    currentPackage = node->packageName;
    functionLabels.clear();
    duplicateLabels.clear();
    currentLocals.clear();
    if (!currentStruct.empty())
    {
        // 方法：注册 this（指向当前结构体实例）
        auto thisSym = std::make_shared<Symbol>("this", SymbolKind::PARAMETER,
                                                SymbolMutability::VAL, currentStruct,
                                                node->line, node->column);
        symbols.declare("this", thisSym);
    }

    // 参数
    for (auto& param : node->params)
    {
        if (param->type && param->type->baseType == ASTNodeType::TYPE_PRIMITIVE)
        {
            tryResolveGenericType(param->type->name);
        }
        if (param->type)
        {
            // 记录指针参数指向类型（buf[i] 用）
            if (param->type->baseType == ASTNodeType::TYPE_POINTER && param->type->inner)
            {
                pointerElementTypes[param->name] = typeNodeToName(param->type->inner.get());
            }
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
        // 预扫描 label（支持前向 goto）
        collectLabels(node->body.get());
        visitBlock(node->body.get());
    }

    symbols.popScope();
    currentReturnType.clear();
    currentFunctionName.clear();
    currentPackage.clear();
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
        if (member->type == ASTNodeType::FUNCTION_DECL)
        {
            // 方法体：以当前结构体为 this 作用域检查
            std::string saved = currentStruct;
            currentStruct = node->name;
            visitFunctionDecl(dynamic_cast<FunctionDeclNode*>(member.get()));
            currentStruct = saved;
            continue;
        }
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
                    // 泛型参数（T）跳过
                    bool isTypeParam = false;
                    for (auto& tp : node->typeParams) if (tp == typeName) isTypeParam = true;
                    if (!isTypeParam && !isBuiltinType(typeName))
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
        return;
    }
    // 方法注册 + 体检查（带 this）
    for (auto& member : node->members)
    {
        if (member->type == ASTNodeType::FUNCTION_DECL)
        {
            auto* fn = dynamic_cast<FunctionDeclNode*>(member.get());
            std::vector<std::string> pt;
            for (auto& p : fn->params)
                pt.push_back(p->type ? typeNodeToName(p->type.get()) : "");
            structRegistry[target].methods[fn->name] =
                { fn->returnType ? typeNodeToName(fn->returnType.get()) : "", std::move(pt) };
            std::string saved = currentStruct;
            currentStruct = target;
            visitFunctionDecl(fn);
            currentStruct = saved;
        }
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
        case ASTNodeType::GOTO_STMT:
            visitGoto(dynamic_cast<GotoStmtNode*>(node));
            break;
        case ASTNodeType::LABEL_STMT:
            visitLabel(dynamic_cast<LabelStmtNode*>(node));
            break;
        case ASTNodeType::SWITCH_STMT:
            visitSwitch(dynamic_cast<SwitchStmtNode*>(node));
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

// ===== 泛型实例化 =====

std::unique_ptr<TypeNode> Sema::substituteType(TypeNode* t,
    const std::vector<std::string>& params, const std::vector<std::string>& args)
{
    if (!t) return nullptr;
    if (t->baseType == ASTNodeType::TYPE_PRIMITIVE)
    {
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (t->name == params[i])
            {
                return std::make_unique<TypeNode>(ASTNodeType::TYPE_PRIMITIVE, args[i],
                                                  t->line, t->column);
            }
        }
        return std::make_unique<TypeNode>(ASTNodeType::TYPE_PRIMITIVE, t->name, t->line, t->column);
    }
    if (t->baseType == ASTNodeType::TYPE_POINTER || t->baseType == ASTNodeType::TYPE_ARRAY)
    {
        auto inner = substituteType(t->inner.get(), params, args);
        return std::make_unique<TypeNode>(t->baseType, "", t->line, t->column,
                                          t->arraySize, std::move(inner), t->isConst);
    }
    return std::make_unique<TypeNode>(t->baseType, t->name, t->line, t->column,
                                      t->arraySize, nullptr, t->isConst);
}

void Sema::instantiateGeneric(const std::string& mangled)
{
    if (structRegistry.count(mangled)) return; // 已实例化
    size_t lt = mangled.find('<');
    if (lt == std::string::npos) return;
    std::string base = mangled.substr(0, lt);
    auto tIt = genericTemplates.find(base);
    if (tIt == genericTemplates.end()) return;
    StructDeclNode* tmpl = tIt->second;

    std::string argStr = mangled.substr(lt + 1, mangled.rfind('>') - lt - 1);
    std::vector<std::string> args;
    size_t pos = 0;
    while (pos <= argStr.size())
    {
        size_t comma = argStr.find(',', pos);
        std::string a = (comma == std::string::npos) ? argStr.substr(pos) : argStr.substr(pos, comma - pos);
        while (!a.empty() && a.front() == ' ') a = a.substr(1);
        while (!a.empty() && a.back() == ' ') a.pop_back();
        if (!a.empty()) args.push_back(a);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (args.size() != tmpl->typeParams.size()) return;

    auto clone = std::make_unique<StructDeclNode>(mangled, tmpl->isAbstract, tmpl->line, tmpl->column);
    clone->isUnion = tmpl->isUnion;
    clone->alignBytes = tmpl->alignBytes;
    clone->bases = tmpl->bases;
    for (auto& m : tmpl->members)
    {
        if (m->type == ASTNodeType::VARIABLE_DECL)
        {
            auto* f = dynamic_cast<VariableDeclNode*>(m.get());
            auto nt = f->type ? substituteType(f->type.get(), tmpl->typeParams, args) : nullptr;
            clone->members.push_back(std::make_unique<VariableDeclNode>(
                f->isVar, f->isMoved, f->name, std::move(nt), nullptr,
                f->line, f->column, f->bitWidth, f->isVolatile));
        }
    }

    auto sym = std::make_shared<Symbol>(mangled, SymbolKind::STRUCT, SymbolMutability::VAL,
                                        "type", (int)mangled.size(), 0);
    symbols.declareGlobal(mangled, sym); // 根作用域，跨函数可见
    StructInfo info;
    for (auto& m : clone->members)
    {
        if (m->type == ASTNodeType::VARIABLE_DECL)
        {
            auto* f = dynamic_cast<VariableDeclNode*>(m.get());
            if (f->type)
            {
                info.fields.emplace_back(f->name, typeNodeToName(f->type.get()));
                if (f->type->baseType == ASTNodeType::TYPE_ARRAY && f->type->inner)
                {
                    info.fieldElementTypes[f->name] = typeNodeToName(f->type->inner.get());
                }
            }
        }
    }
    structRegistry[mangled] = std::move(info);

    if (currentProgram) currentProgram->decls.push_back(std::move(clone));
}

bool Sema::tryResolveGenericType(const std::string& name)
{
    if (name.find('<') == std::string::npos) return true;
    instantiateGeneric(name);
    return structRegistry.count(name) > 0;
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

        // 记录数组元素类型（buf[i] 用）
        if (node->type->baseType == ASTNodeType::TYPE_ARRAY && node->type->inner)
        {
            arrayElementTypes[node->name] = typeNodeToName(node->type->inner.get());
        }
        // 记录指针指向类型（buf[i] 用）
        if (node->type->baseType == ASTNodeType::TYPE_POINTER && node->type->inner)
        {
            pointerElementTypes[node->name] = typeNodeToName(node->type->inner.get());
        }

        // 检查用户自定义类型（IDENT）是否已定义
        if (node->type->baseType == ASTNodeType::TYPE_PRIMITIVE && !declaredType.empty())
        {
            if (!isBuiltinType(declaredType))
            {
                tryResolveGenericType(declaredType); // 泛型实例化（如 Box<int>）
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
    else
    {
        currentLocals.insert(node->name); // 借用检查：局部变量
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

void Sema::collectLabels(ASTNode* node)
{
    if (!node) return;
    if (node->type == ASTNodeType::LABEL_STMT)
    {
        functionLabels.insert(dynamic_cast<LabelStmtNode*>(node)->name);
        return;
    }
    if (node->type == ASTNodeType::BLOCK_STMT)
    {
        for (auto& st : dynamic_cast<BlockStmtNode*>(node)->statements) collectLabels(st.get());
    }
    else if (node->type == ASTNodeType::IF_STMT)
    {
        auto* ifn = dynamic_cast<IfStmtNode*>(node);
        collectLabels(ifn->thenBranch.get());
        collectLabels(ifn->elseBranch.get());
    }
    else if (node->type == ASTNodeType::WHILE_STMT)
    {
        collectLabels(dynamic_cast<WhileStmtNode*>(node)->body.get());
    }
    else if (node->type == ASTNodeType::FOR_STMT)
    {
        auto* fn = dynamic_cast<ForStmtNode*>(node);
        collectLabels(fn->init.get());
        collectLabels(fn->body.get());
    }
    else if (node->type == ASTNodeType::SWITCH_STMT)
    {
        for (auto& c : dynamic_cast<SwitchStmtNode*>(node)->cases) collectLabels(c.body.get());
    }
}

void Sema::visitGoto(GotoStmtNode* node)
{
    if (functionLabels.find(node->label) == functionLabels.end())
    {
        error(node->line, node->column, "undefined label '" + node->label + "'");
    }
}

void Sema::visitLabel(LabelStmtNode* node)
{
    if (!duplicateLabels.insert(node->name).second)
    {
        error(node->line, node->column, "duplicate label '" + node->name + "'");
    }
}

void Sema::visitSwitch(SwitchStmtNode* node)
{
    std::string condType = visitExpr(node->condition.get());
    if (!condType.empty() && !isNumericType(condType))
    {
        error(node->line, node->column, "switch condition must be numeric, got '" + condType + "'");
    }
    std::set<long long> seen;
    for (auto& c : node->cases)
    {
        if (!c.isDefault)
        {
            if (!seen.insert(c.value).second)
            {
                error(node->line, node->column, "duplicate case value " + std::to_string(c.value));
            }
        }
        if (c.body) visitStmt(c.body.get());
    }
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
        // 借用检查：返回局部变量地址 → 悬垂指针
        if (currentReturnType == "pointer" && node->value->type == ASTNodeType::UNARY_OP)
        {
            if (auto* addr = dynamic_cast<AddressOfNode*>(node->value.get()))
            {
                std::string root = rootVarName(addr->operand.get());
                if (!root.empty() && currentLocals.count(root))
                {
                    error(node->line, node->column, "cannot return reference to local variable '" +
                          root + "' (dangling pointer)");
                }
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
            // 取地址 &a → pointer；&函数名 → func（函数指针）
            if (auto* addr = dynamic_cast<AddressOfNode*>(node))
            {
                if (addr->operand->type == ASTNodeType::VARIABLE_REF)
                {
                    auto* ref = dynamic_cast<VariableRefNode*>(addr->operand.get());
                    auto fnSym = symbols.lookup(ref->name);
                    if (fnSym && fnSym->kind == SymbolKind::FUNCTION)
                    {
                        return "func";
                    }
                }
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
        case ASTNodeType::LITERAL_NULL: return "pointer";
        case ASTNodeType::VARIABLE_REF: return visitVariableRef(dynamic_cast<VariableRefNode*>(node));
        case ASTNodeType::INDEX: return visitIndex(dynamic_cast<IndexNode*>(node));
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
        // 指针算术：pointer +- int → pointer（按元素大小缩放）
        bool leftPtr = leftType == "pointer" || leftType == "ptr";
        bool rightPtr = rightType == "pointer" || rightType == "ptr";
        if ((leftPtr && isNumericType(rightType)) || (rightPtr && isNumericType(leftType)))
        {
            return leftPtr ? leftType : rightType;
        }
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
        if (operandType != "pointer" && operandType != "ptr" && !operandType.empty())
        {
            error(node->line, node->column, "cannot dereference non-pointer type '" + operandType + "'");
        }
        // 类型化解引用：指针变量的指向类型
        if (node->operand->type == ASTNodeType::VARIABLE_REF)
        {
            auto* ref = dynamic_cast<VariableRefNode*>(node->operand.get());
            auto it = pointerElementTypes.find(ref->name);
            if (it != pointerElementTypes.end()) return it->second;
        }
        return "int"; // 简化：未知元素类型按 int
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
    // std.thread 编译器内置调用（仅 spawn / sleep / mutex.create；其余为 std.thread 源码库函数）
    if (node->name == "thread.spawn" || node->name == "thread.sleep" ||
        node->name == "thread.mutex.create")
    {
        return visitThreadCall(node);
    }

    // std.atomic 原子内置调用
    if (node->name.rfind("atomic.", 0) == 0)
    {
        return visitAtomicCall(node);
    }

    // 成员方法调用 circle.area() / 包限定调用 thread.join() —— 拆分根对象与方法名
    size_t dot = node->name.find('.');
    if (dot != std::string::npos)
    {
        std::string objName = node->name.substr(0, dot);

        // 包限定调用：objName 是某个已导入包的别名（如 import std.thread 后的 thread）
        bool isPackageAlias = false;
        for (const auto& imp : importedModules)
        {
            std::string alias = imp.substr(imp.rfind('.') + 1);
            if (alias == objName) { isPackageAlias = true; break; }
        }
        if (isPackageAlias)
        {
            auto pkgSym = symbols.lookup(node->name); // "<别名>.<函数名>" 已在注册阶段登记
            if (!pkgSym)
            {
                error(node->line, node->column,
                      "unknown function '" + node->name + "' in module '" + objName + "'");
                return "";
            }
            if (pkgSym->kind != SymbolKind::FUNCTION)
            {
                error(node->line, node->column, "'" + node->name + "' is not a function");
                return "";
            }
            // 跨包可见性：必须 pub（同包调用豁免）
            if (!pkgSym->isPub && pkgSym->packageName != currentPackage)
            {
                error(node->line, node->column, "function '" + node->name + "' is not public");
                return "";
            }
            // 参数数量检查（变参函数允许至少 N 个）
            if (!pkgSym->paramTypes.empty() || !node->arguments.empty())
            {
                if (pkgSym->isVariadic)
                {
                    if (node->arguments.size() < pkgSym->paramTypes.size())
                    {
                        error(node->line, node->column, "function '" + node->name + "' expects at least " +
                              std::to_string(pkgSym->paramTypes.size()) + " argument(s), got " +
                              std::to_string(node->arguments.size()));
                    }
                }
                else if (pkgSym->paramTypes.size() != node->arguments.size())
                {
                    error(node->line, node->column, "function '" + node->name + "' expects " +
                          std::to_string(pkgSym->paramTypes.size()) + " argument(s), got " +
                          std::to_string(node->arguments.size()));
                }
            }
            // 参数类型检查
            for (size_t i = 0; i < node->arguments.size() && i < pkgSym->paramTypes.size(); ++i)
            {
                std::string argType = visitExpr(node->arguments[i].get());
                if (!argType.empty() && !pkgSym->paramTypes[i].empty())
                {
                    if (!isCompatible(argType, pkgSym->paramTypes[i]))
                    {
                        error(node->line, node->column, "argument " + std::to_string(i + 1) + " of '" +
                              node->name + "' expects '" + pkgSym->paramTypes[i] + "', got '" + argType + "'");
                    }
                }
            }
            // 重命名为函数本名（合并单模块代码生成直接按本名查 LLVM 函数）
            node->name = pkgSym->name;
            return pkgSym->returnType;
        }

        std::string methodName = node->name.substr(dot + 1);

        auto objSym = symbols.lookup(objName);
        if (!objSym)
        {
            error(node->line, node->column, "call to method on undefined object '" + objName + "'");
            return "";
        }
        // 静态方法调用 obj.method(args)：查结构体方法表
        auto st = structRegistry.find(objSym->typeName);
        if (st != structRegistry.end())
        {
            auto mIt = st->second.methods.find(methodName);
            if (mIt != st->second.methods.end())
            {
                auto& [retType, paramTypes] = mIt->second;
                if (node->arguments.size() != paramTypes.size())
                {
                    error(node->line, node->column, "method '" + methodName + "' expects " +
                          std::to_string(paramTypes.size()) + " argument(s), got " +
                          std::to_string(node->arguments.size()));
                }
                for (size_t i = 0; i < node->arguments.size() && i < paramTypes.size(); ++i)
                {
                    std::string at = visitExpr(node->arguments[i].get());
                    if (!at.empty() && !paramTypes[i].empty() && !isCompatible(at, paramTypes[i]))
                    {
                        error(node->line, node->column, "argument " + std::to_string(i + 1) + " of method '" +
                              methodName + "' expects '" + paramTypes[i] + "', got '" + at + "'");
                    }
                }
                return retType;
            }
        }
        // 非方法：按成员字段访问检查
        for (auto& arg : node->arguments)
        {
            visitExpr(arg.get());
        }
        error(node->line, node->column, "struct '" + objSym->typeName + "' has no method '" + methodName + "'");
        return "";
    }

    auto sym = symbols.lookup(node->name);
    if (!sym)
    {
        error(node->line, node->column, "call to undefined function '" + node->name + "'");
        return "";
    }
    // 间接调用：函数指针变量/参数 cb(...)
    if (sym->kind == SymbolKind::VARIABLE || sym->kind == SymbolKind::PARAMETER)
    {
        bool isFuncPtr = sym->typeName == "func";
        auto pe = pointerElementTypes.find(node->name);
        if (pe != pointerElementTypes.end() && pe->second == "func") isFuncPtr = true;
        if (isFuncPtr)
        {
            for (auto& a : node->arguments) visitExpr(a.get());
            return ""; // v1：不跟踪签名，视为无返回
        }
    }
    if (sym->kind != SymbolKind::FUNCTION)
    {
        error(node->line, node->column, "'" + node->name + "' is not a function");
        return "";
    }
    // 跨包可见性：非 pub 的跨包函数不可直接调用
    if (!sym->isPub && !sym->packageName.empty() && sym->packageName != currentPackage)
    {
        error(node->line, node->column, "function '" + node->name + "' is not public");
        return "";
    }

    // 参数数量检查（变参函数允许至少 N 个）
    if (!sym->paramTypes.empty() || !node->arguments.empty())
    {
        if (sym->isVariadic)
        {
            if (node->arguments.size() < sym->paramTypes.size())
            {
                error(node->line, node->column, "function '" + node->name + "' expects at least " +
                      std::to_string(sym->paramTypes.size()) + " argument(s), got " +
                      std::to_string(node->arguments.size()));
            }
        }
        else if (sym->paramTypes.size() != node->arguments.size())
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

// std.thread 编译器内置（仅 spawn / sleep / mutex.create）

std::string Sema::visitThreadCall(FunctionCallNode* node)
{
    if (importedModules.find("std.thread") == importedModules.end())
    {
        error(node->line, node->column,
              "module 'std.thread' is not imported (add 'import std.thread;')");
        return "";
    }

    const std::string& full = node->name;
    const size_t argCount = node->arguments.size();

    auto checkArgCount = [&](size_t expected) -> bool
    {
        if (argCount == expected) return true;
        error(node->line, node->column, "function '" + full + "' expects " +
              std::to_string(expected) + " argument(s), got " + std::to_string(argCount));
        return false;
    };

    auto isNumeric = [&](const std::string& t) -> bool
    {
        return isNumericType(t);
    };

    // thread.spawn(fn)：启动线程运行无参函数，返回线程句柄（pointer）
    if (full == "thread.spawn")
    {
        // 一参：无参函数；两参：带一个指针参数的共享内存线程
        if (argCount < 1 || argCount > 2)
        {
            error(node->line, node->column,
                  "thread.spawn expects 1 argument (function) or 2 (function, shared pointer)");
            return "";
        }
        auto* ref = dynamic_cast<VariableRefNode*>(node->arguments[0].get());
        std::string fnName = ref ? ref->name : "";
        if (fnName.empty() || fnName.find('.') != std::string::npos)
        {
            error(node->line, node->column, "thread.spawn expects a function name");
            return "";
        }
        auto fnSym = symbols.lookup(fnName);
        if (!fnSym || fnSym->kind != SymbolKind::FUNCTION)
        {
            error(node->line, node->column, "thread.spawn expects a function name, got '" + fnName + "'");
            return "";
        }
        if (argCount == 2)
        {
            // 共享内存：目标函数必须恰好一个指针参数，第二参数为指针
            if (fnSym->paramTypes.size() != 1)
            {
                error(node->line, node->column,
                      "thread.spawn shared function '" + fnName + "' must take exactly 1 pointer parameter");
                return "";
            }
            std::string argType = visitExpr(node->arguments[1].get());
            if (argType != "pointer" && argType != "ptr")
            {
                error(node->line, node->column,
                      "thread.spawn second argument must be a pointer, got '" + argType + "'");
                return "";
            }
        }
        else if (!fnSym->paramTypes.empty())
        {
            error(node->line, node->column,
                  "thread.spawn function '" + fnName + "' must not take parameters");
            return "";
        }
        return "pointer";
    }

    // thread.sleep(ms)：当前线程休眠指定毫秒
    if (full == "thread.sleep")
    {
        if (!checkArgCount(1)) return "";
        std::string ms = visitExpr(node->arguments[0].get());
        if (!ms.empty() && !isNumeric(ms))
        {
            error(node->line, node->column,
                  "thread.sleep expects a numeric duration in milliseconds, got '" + ms + "'");
        }
        return "";
    }

    // thread.mutex.create()：从池中分配并初始化一把互斥锁，返回其地址（pointer）
    if (full == "thread.mutex.create")
    {
        if (!checkArgCount(0)) return "";
        return "pointer";
    }

    error(node->line, node->column, "unknown function '" + full + "' in module std.thread");
    return "";
}

// ===== std.atomic 编译器内置 =====

std::string Sema::visitAtomicCall(FunctionCallNode* node)
{
    if (importedModules.find("std.atomic") == importedModules.end())
    {
        error(node->line, node->column,
              "module 'std.atomic' is not imported (add 'import std.atomic;')");
        return "";
    }
    const std::string& full = node->name;
    const size_t n = node->arguments.size();

    // 第一个参数必须是指针且已知指向整数类型
    std::string elemType;
    if (n >= 1 && node->arguments[0]->type == ASTNodeType::VARIABLE_REF)
    {
        auto* ref = dynamic_cast<VariableRefNode*>(node->arguments[0].get());
        auto it = pointerElementTypes.find(ref->name);
        if (it != pointerElementTypes.end()) elemType = it->second;
    }
    else
    {
        visitExpr(node->arguments[0].get());
    }
    if (elemType.empty() || !isNumericType(elemType))
    {
        error(node->line, node->column,
              "'" + full + "' expects a pointer to an integer type");
        return "";
    }

    // 固定参数个数：load/exchange=1，store/add/sub=2，cas=3；可带一个内存序参数
    size_t fixed;
    if (full == "atomic.load" || full == "atomic.exchange") fixed = 1;
    else if (full == "atomic.store" || full == "atomic.add" || full == "atomic.sub") fixed = 2;
    else if (full == "atomic.cas") fixed = 3;
    else
    {
        error(node->line, node->column, "unknown function '" + full + "' in module std.atomic");
        return "";
    }
    if (n < fixed || n > fixed + 1)
    {
        error(node->line, node->column, "function '" + full + "' expects " +
              std::to_string(fixed) + " argument(s) plus optional memory order, got " + std::to_string(n));
        return "";
    }
    for (size_t i = 1; i < fixed; ++i) visitExpr(node->arguments[i].get());
    if (n == fixed + 1)
    {
        std::string ot = visitExpr(node->arguments[n - 1].get());
        if (!ot.empty() && !isNumericType(ot))
        {
            error(node->line, node->column, "memory order must be an integer (0-4), got '" + ot + "'");
        }
    }

    if (full == "atomic.store") return "";
    if (full == "atomic.cas") return "bool";
    return elemType; // load/add/sub/exchange 返回旧值/当前值，类型为元素类型
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
    // 结构体成员访问 s.x / a.b.c：逐级解析成员类型
    size_t dot = node->name.find('.');
    if (dot != std::string::npos)
    {
        std::string curType = sym->typeName;
        std::string rest = node->name.substr(dot + 1);
        while (!rest.empty())
        {
            size_t next = rest.find('.');
            std::string member = (next == std::string::npos) ? rest : rest.substr(0, next);
            rest = (next == std::string::npos) ? "" : rest.substr(next + 1);
            auto st = structRegistry.find(curType);
            if (st == structRegistry.end())
            {
                error(node->line, node->column, "'" + curType + "' is not a struct");
                return "";
            }
            bool found = false;
            for (auto& f : st->second.fields)
            {
                if (f.first == member) { curType = f.second; found = true; break; }
            }
            if (!found)
            {
                error(node->line, node->column, "struct '" + curType + "' has no member '" + member + "'");
                return "";
            }
        }
        return curType;
    }
    return sym->typeName;
}

std::string Sema::visitIndex(IndexNode* node)
{
    // 数组下标 buf[i]：支持数组变量与指针变量（含参数）
    std::string elemType;
    if (node->operand->type == ASTNodeType::VARIABLE_REF)
    {
        auto* ref = dynamic_cast<VariableRefNode*>(node->operand.get());
        // 结构体数组成员 s.arr[i]
        size_t mdot = ref->name.find('.');
        if (mdot != std::string::npos)
        {
            std::string root = ref->name.substr(0, mdot);
            std::string member = ref->name.substr(mdot + 1);
            auto rsym = symbols.lookup(root);
            if (rsym)
            {
                auto st = structRegistry.find(rsym->typeName);
                if (st != structRegistry.end())
                {
                    auto eIt = st->second.fieldElementTypes.find(member);
                    if (eIt != st->second.fieldElementTypes.end())
                    {
                        std::string idxType = visitExpr(node->index.get());
                        if (!idxType.empty() && !isNumericType(idxType))
                        {
                            error(node->line, node->column, "array index must be an integer, got '" + idxType + "'");
                        }
                        return eIt->second;
                    }
                }
            }
        }
        auto aIt = arrayElementTypes.find(ref->name);
        auto pIt = pointerElementTypes.find(ref->name);
        if (aIt != arrayElementTypes.end())
        {
            elemType = aIt->second;
        }
        else if (pIt != pointerElementTypes.end())
        {
            elemType = pIt->second;
        }
        else
        {
            auto sym = symbols.lookup(ref->name);
            error(node->line, node->column,
                  sym ? "variable '" + ref->name + "' is not an array or pointer"
                      : "undefined variable '" + ref->name + "'");
        }
    }
    else
    {
        error(node->line, node->column, "indexing requires an array or pointer variable");
    }

    std::string idxType = visitExpr(node->index.get());
    if (!idxType.empty() && !isNumericType(idxType))
    {
        error(node->line, node->column, "array index must be an integer, got '" + idxType + "'");
    }
    return elemType;
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

// 取地址表达式的根变量名（&a.b[0] → "a"；&函数名 → 空）
std::string Sema::rootVarName(ASTNode* node)
{
    if (!node) return "";
    if (node->type == ASTNodeType::VARIABLE_REF)
    {
        if (auto* ref = dynamic_cast<VariableRefNode*>(node)) return ref->name;
    }
    if (node->type == ASTNodeType::VARIABLE_REF || node->type == ASTNodeType::FUNCTION_CALL)
    {
        if (auto* ma = dynamic_cast<MemberAccessNode*>(node)) return rootVarName(ma->object.get());
    }
    if (auto* idx = dynamic_cast<IndexNode*>(node)) return rootVarName(idx->operand.get());
    return "";
}

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
           type == "f32" || type == "f64" || type == "char"; // char 即 u8
}

bool Sema::isBuiltinType(const std::string& type) const
{
    return isNumericType(type) || type == "char" || type == "string" ||
           type == "bool" || type == "pointer" || type == "array" ||
           type == "wchar" || type == "wstring" || type == "ptr" || type == "func";
}

bool Sema::isCompatible(const std::string& from, const std::string& to) const
{
    if (from == to) return true;

    // 字符串 → 指针衰减（printf("%s", s) 等）
    if (from == "string" && to == "pointer") return true;

    // ptr 与 pointer 别名；func（函数指针）与 pointer 兼容
    if ((from == "ptr" && to == "pointer") || (from == "pointer" && to == "ptr")) return true;
    if ((from == "func" && to == "pointer") || (from == "pointer" && to == "func")) return true;

    // int/uint 别名
    if ((from == "int" && to == "i32") || (from == "i32" && to == "int")) return true;
    if ((from == "uint" && to == "u32") || (from == "u32" && to == "uint")) return true;

    // 整数提升：小类型可安全赋给大类型
    static const std::vector<std::string> intRank = {
        "i8", "u8", "char", "i16", "u16", "int", "uint", "i32", "u32", "i64", "u64"
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
