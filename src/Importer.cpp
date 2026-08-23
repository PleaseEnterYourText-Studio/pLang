#include "../include/Importer.h"
#include "../include/Lexer.h"
#include "../include/Parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

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

// 真实可执行文件路径（跨平台，兼容软链/PATH 调用）
std::string impGetExecutablePath()
{
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
    {
        char real[4096];
        if (realpath(buf, real) != nullptr) return std::string(real);
        return std::string(buf);
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0)
    {
        buf[n] = '\0';
        return std::string(buf);
    }
#elif defined(_WIN32)
    char buf[4096];
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0) return std::string(buf, n);
#endif
    return "";
}

// 标准库根目录：import std.thread 对应 <root>/std/thread
std::string plangGetStdlibRoot(const std::string& exePath)
{
    if (const char* env = std::getenv("PLANG_STD"))
    {
        return env;
    }
    // 一律用真实可执行文件路径（LSP 无 argv[0] 也能定位到 std/；软链/PATH 调用同理）
    std::string exe = impGetExecutablePath();
    if (exe.empty()) exe = exePath;
    if (exe.empty())
    {
        // 兜底：当前工作目录
        return fs::current_path().string();
    }
    fs::path exeAbs;
    try
    {
        exeAbs = fs::canonical(fs::path(exe));
    }
    catch (...)
    {
        exeAbs = fs::absolute(fs::path(exe));
    }
    return exeAbs.parent_path().parent_path().string();
}

// 用户包根（pvp 安装的第三方包）：PLANG_PVP 覆盖，否则各平台用户数据目录
std::string plangGetPvpRoot()
{
    if (const char* env = std::getenv("PLANG_PVP"))
    {
        return env;
    }
#if defined(_WIN32)
    if (const char* la = std::getenv("LOCALAPPDATA"))
        return std::string(la) + "\\pLang\\0.x\\packages";
    if (const char* up = std::getenv("USERPROFILE"))
        return std::string(up) + "\\AppData\\Local\\pLang\\0.x\\packages";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/Library/Application Support/pLang/0.x/packages";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        if (*xdg) return std::string(xdg) + "/pLang/0.x/packages";
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/.local/share/pLang/0.x/packages";
#endif
    return "";
}

// 读 pvp installed.json：别名 → 仓库地址
std::map<std::string, std::string> plangInstalledAliases(const std::string& pvpRoot)
{
    std::map<std::string, std::string> m;
    std::ifstream f(fs::path(pvpRoot) / "installed.json");
    if (!f.is_open()) return m;
    std::stringstream buf;
    buf << f.rdbuf();
    std::string c = buf.str();
    size_t pos = 0;
    while (true)
    {
        size_t q1 = c.find('"', pos);
        if (q1 == std::string::npos) break;
        size_t q2 = c.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string key = c.substr(q1 + 1, q2 - q1 - 1);
        size_t colon = c.find(':', q2);
        size_t q3 = c.find('"', colon);
        size_t q4 = c.find('"', q3 + 1);
        if (colon == std::string::npos || q3 == std::string::npos || q4 == std::string::npos) break;
        m[key] = c.substr(q3 + 1, q4 - q3 - 1);
        pos = q4 + 1;
    }
    return m;
}

// 解析模块目录：标准库 → pvp 直接路径 → installed.json 别名
fs::path plangResolveModuleDir(const std::string& path, const std::string& stdlibRoot)
{
    std::string modPath = path;
    if (modPath.find('/') == std::string::npos)
        std::replace(modPath.begin(), modPath.end(), '.', '/');
    fs::path dir = fs::path(stdlibRoot) / modPath;
    if (fs::is_directory(dir)) return dir;
    std::string pvp = plangGetPvpRoot();
    if (!pvp.empty())
    {
        dir = fs::path(pvp) / modPath;
        if (fs::is_directory(dir)) return dir;
        auto aliases = plangInstalledAliases(pvp);
        auto it = aliases.find(path);
        if (it != aliases.end())
        {
            dir = fs::path(pvp) / it->second;
            if (fs::is_directory(dir)) return dir;
        }
    }
    return fs::path();
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
    decl->typeParams = fn->typeParams;   // 泛型函数模板：调用时实例化
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

    fs::path moduleDir = plangResolveModuleDir(path, stdlibRoot);
    if (moduleDir.empty())
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
        // 包名匹配：完整地址（github.com/user/repo）或短名（repo，Go 式）
        std::string shortName = path;
        size_t lastSlash = path.rfind('/');
        if (lastSlash != std::string::npos) shortName = path.substr(lastSlash + 1);
        if (libProgram->packageName != path && libProgram->packageName != shortName)
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
                if (!fn->typeParams.empty())
                {
                    // 泛型函数模板：整个定义（含函数体）合并进宿主，实例化时克隆生成实现
                    hostProgram->decls.push_back(std::move(decl));
                }
                else if (fn->isPub || fn->isExtern)
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
