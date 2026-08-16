#include "../include/Importer.h"
#include "../include/Lexer.h"
#include "../include/Parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// 解析单个源文件（词法+语法），出错返回 false
bool plangParseSourceFile(const std::string& filename, std::unique_ptr<ProgramNode>& outProgram)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    Lexer lexer(source);
    auto tokens = lexer.scanTokens();
    for (const auto& tok : tokens)
    {
        if (tok.type == TokenType::ERROR)
        {
            return false;
        }
    }

    Parser parser(tokens);
    try
    {
        outProgram = parser.parse();
    }
    catch (...)
    {
        return false;
    }
    return true;
}

// 标准库根目录：import std.thread 对应 <root>/std/thread
std::string plangGetStdlibRoot(const std::string& exePath)
{
    if (const char* env = std::getenv("PLANG_STD"))
    {
        return env;
    }
    if (exePath.empty())
    {
        // LSP 等无 argv[0] 的场景：以当前工作目录为根（通常从项目根启动）
        return fs::current_path().string();
    }
    // 默认：可执行文件所在目录的上一级（build/PLang → 仓库根）；规范化路径以兼容相对 argv[0]
    fs::path exeAbs;
    try
    {
        exeAbs = fs::canonical(fs::path(exePath));
    }
    catch (...)
    {
        exeAbs = fs::absolute(fs::path(exePath));
    }
    return exeAbs.parent_path().parent_path().string();
}

// 深拷贝 TypeNode
std::unique_ptr<TypeNode> plangCloneType(TypeNode* t)
{
    if (!t) return nullptr;
    if (t->baseType == ASTNodeType::TYPE_POINTER || t->baseType == ASTNodeType::TYPE_ARRAY)
    {
        return std::make_unique<TypeNode>(t->baseType, t->name, t->line, t->column,
                                          t->arraySize, plangCloneType(t->inner.get()), t->isConst);
    }
    return std::make_unique<TypeNode>(t->baseType, t->name, t->line, t->column,
                                      t->arraySize, nullptr, t->isConst);
}

// 把库的函数克隆为 extern 声明（独立编译：定义留在库包 .o）
static void injectExternFunction(ProgramNode* host, FunctionDeclNode* fn, const std::string& name)
{
    auto decl = std::make_unique<FunctionDeclNode>(name, fn->line, fn->column);
    decl->isExtern = true;
    decl->isPub = true;
    decl->isVariadic = fn->isVariadic;
    decl->packageName = fn->packageName;
    for (auto& p : fn->params)
    {
        decl->params.push_back(std::make_unique<ParameterNode>(
            p->isVar, p->name, plangCloneType(p->type.get()), p->line, p->column));
    }
    if (fn->returnType) decl->returnType = plangCloneType(fn->returnType.get());
    host->decls.push_back(std::move(decl));
}

// 递归解析导入的包并合并其声明到宿主程序（带环检测）
static void resolveModule(ProgramNode* hostProgram, const std::string& path, const std::string& stdlibRoot,
                          std::set<std::string>& resolved, std::vector<std::string>& importStack,
                          bool& errorFlag, std::vector<std::string>* resolvedPackages)
{
    if (resolved.count(path)) return;

    // 环检测：import 链上再次出现同一模块
    for (const auto& s : importStack)
    {
        if (s == path)
        {
            errorFlag = true;
            return;
        }
    }

    std::string modPath = path;
    std::replace(modPath.begin(), modPath.end(), '.', '/');
    fs::path moduleDir = fs::path(stdlibRoot) / modPath;
    if (!fs::is_directory(moduleDir))
    {
        resolved.insert(path); // 模块不存在：静默忽略
        return;
    }

    importStack.push_back(path);
    for (const auto& entry : fs::directory_iterator(moduleDir))
    {
        if (entry.path().extension() != ".plang") continue;

        std::unique_ptr<ProgramNode> libProgram;
        if (!plangParseSourceFile(entry.path().string(), libProgram))
        {
            errorFlag = true;
            continue;
        }
        if (libProgram->packageName != path)
        {
            errorFlag = true;
            continue;
        }
        for (auto& imp : libProgram->imports)
        {
            auto* importNode = dynamic_cast<ImportStmtNode*>(imp.get());
            if (importNode)
            {
                resolveModule(hostProgram, importNode->path, stdlibRoot, resolved, importStack,
                              errorFlag, resolvedPackages);
            }
        }
        for (auto& decl : libProgram->decls)
        {
            // 独立编译：函数注入 extern 声明（定义在库包 .o）；类型/结构体合并
            if (decl->type == ASTNodeType::FUNCTION_DECL)
            {
                auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get());
                if (fn->isPub || fn->isExtern)
                {
                    injectExternFunction(hostProgram, fn, fn->name);
                }
            }
            else if (decl->type == ASTNodeType::STRUCT_DECL ||
                     decl->type == ASTNodeType::USING_DECL)
            {
                // 结构体方法 → extern 声明（方法定义在库包 .o）；先取指针再 move
                auto* sn = dynamic_cast<StructDeclNode*>(
                    (decl->type == ASTNodeType::STRUCT_DECL) ? decl.get()
                        : dynamic_cast<UsingDeclNode*>(decl.get())->aliased.get());
                hostProgram->decls.push_back(std::move(decl));
                if (sn)
                {
                    for (auto& m : sn->members)
                    {
                        if (m->type == ASTNodeType::FUNCTION_DECL)
                        {
                            auto* fn = dynamic_cast<FunctionDeclNode*>(m.get());
                            injectExternFunction(hostProgram, fn, sn->name + "." + fn->name);
                        }
                    }
                }
            }
            else
            {
                hostProgram->decls.push_back(std::move(decl));
            }
        }
        for (auto& imp : libProgram->imports)
        {
            hostProgram->imports.push_back(std::move(imp));
        }
    }
    importStack.pop_back();
    resolved.insert(path);
    if (resolvedPackages) resolvedPackages->push_back(path);
}

void plangResolveImports(ProgramNode* hostProgram, const std::string& stdlibRoot, bool& errorFlag,
                         std::vector<std::string>* resolvedPackages)
{
    std::set<std::string> resolved;
    std::vector<std::string> importStack;
    std::vector<std::string> paths;
    for (auto& imp : hostProgram->imports)
    {
        auto* importNode = dynamic_cast<ImportStmtNode*>(imp.get());
        if (importNode) paths.push_back(importNode->path);
    }
    for (const auto& path : paths)
    {
        resolveModule(hostProgram, path, stdlibRoot, resolved, importStack, errorFlag, resolvedPackages);
    }
}
