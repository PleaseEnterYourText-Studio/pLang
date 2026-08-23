#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <algorithm>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
#include <set>
#include <filesystem>
#include "llvm/Support/JSON.h"
#include "Lexer.h"
#include "Parser.h"
#include "Sema.h"
#include "CodeGenerator.h"
#include "AST.h"
#include "token.h"
#include "Importer.h"

namespace fs = std::filesystem;

// 工具函数（保持不变）
std::string getLine(const std::string& source, int line) {
    std::istringstream stream(source);
    std::string lineText;
    for (int i = 1; i <= line; ++i) {
        if (!std::getline(stream, lineText)) {
            return "";
        }
    }
    return lineText;
}

void printError(const std::string& filename, const std::string& source, int line, int column,
                const std::string& message) {
    std::cerr << filename << ":" << line << ":" << column << ":\n";
    std::cerr << "  error: " << message << "\n";
    std::string lineText = getLine(source, line);
    if (!lineText.empty()) {
        std::cerr << "  " << line << " | " << lineText << "\n";
        std::cerr << "  " << std::string(std::to_string(line).size(), ' ') << " | " 
                  << std::string(column - 1, ' ') << "^" << "\n";
    }
}

void printWarning(const std::string& filename, const std::string& source, int line, int column,
                  const std::string& message) {
    std::cerr << filename << ":" << line << ":" << column << ":\n";
    std::cerr << "  warning: " << message << "\n";
    std::string lineText = getLine(source, line);
    if (!lineText.empty()) {
        std::cerr << "  " << line << " | " << lineText << "\n";
        std::cerr << "  " << std::string(std::to_string(line).size(), ' ') << " | " 
                  << std::string(column - 1, ' ') << "^" << "\n";
    }
}

std::string withExtension(const std::string& path, const std::string& newExt) {
    fs::path p(path);
    return p.replace_extension(newExt).string();
}

// 收集 .plang 文件
std::vector<std::string> collectPlangFiles(const std::vector<std::string>& inputs) {
    std::vector<std::string> files;
    
    for (const auto& input : inputs) {
        if (fs::is_directory(input)) {
            for (const auto& entry : fs::directory_iterator(input)) {
                if (entry.path().extension() == ".plang") {
                    files.push_back(entry.path().string());
                }
            }
        } else if (fs::exists(input) && fs::path(input).extension() == ".plang") {
            files.push_back(input);
        }
    }
    
    return files;
}

// 链接可执行文件
bool linkExecutable(const std::vector<std::string>& objFiles, const std::string& exePath, bool needSqlite,
                    const std::vector<std::string>& extraLibs = {}) {
    if (objFiles.empty()) {
        std::cerr << "error: no object files to link\n";
        return false;
    }
    
    std::string cmd;
#if defined(__APPLE__)
    // 用 clang 驱动链接：裸 ld 不生成 debug map，dsymutil 无法提取 .o 的 DWARF
    cmd = "clang -o " + exePath;
    for (const auto& obj : objFiles) cmd += " " + obj;
    if (needSqlite) cmd += " -lsqlite3";
    for (const auto& lib : extraLibs) cmd += " -l" + lib;
#elif defined(__linux__)
    // Linux 需要 crt 文件，用 gcc 驱动更简单；多线程需要链接 pthread
    cmd = "g++ -o " + exePath;
    for (const auto& obj : objFiles) cmd += " " + obj;
    cmd += " -pthread";
    if (needSqlite) cmd += " -lsqlite3";
    for (const auto& lib : extraLibs) cmd += " -l" + lib;
#elif defined(_WIN32)
    // Windows：用 clang 驱动（自动处理 CRT 与入口），对象文件 .obj
    cmd = "clang++ -o " + exePath;
    for (const auto& obj : objFiles) cmd += " " + obj;
    if (needSqlite) cmd += " -lsqlite3";
    for (const auto& lib : extraLibs) cmd += " -l" + lib;
#else
    std::cerr << "error: unsupported platform for linking\n";
    return false;
#endif
    
    bool ok = std::system(cmd.c_str()) == 0;
#if defined(__APPLE__)
    // 生成 dSYM：macOS 链接产物本身不含 DWARF，调试器需读 dSYM
    if (ok)
    {
        std::string dsym = "dsymutil " + exePath + " >/dev/null 2>&1";
        std::system(dsym.c_str());
    }
#endif
    return ok;
}

// 标准库根目录
// import std.thread 对应目录 <root>/std/thread，故 root 为包层级根（仓库根）。
// 真实可执行文件路径（跨平台，兼容软链/PATH 调用）
// macOS: _NSGetExecutablePath；Linux: /proc/self/exe；Windows: GetModuleFileName
std::string getExecutablePath()
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

std::string getStdlibRoot(const std::string& exePath)
{
    if (const char* env = std::getenv("PLANG_STD"))
    {
        return env;
    }
    // 用真实可执行文件路径（软链/PATH 调用也能定位到包内 std/）
    std::string exe = getExecutablePath();
    if (exe.empty()) exe = exePath;
    fs::path exeAbs;
    try
    {
        exeAbs = fs::canonical(fs::path(exe));
    }
    catch (...)
    {
        exeAbs = fs::absolute(fs::path(exe));
    }
    fs::path exeDir = exeAbs.parent_path();
    return exeDir.parent_path().string();
}

// 解析单个源文件（词法+语法），出错返回 false
bool parseSourceFile(const std::string& filename, std::unique_ptr<ProgramNode>& outProgram)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << filename << ": error: cannot open file\n";
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
            printError(filename, source, tok.line, tok.column, tok.text);
            return false;
        }
    }

    Parser parser(tokens);
    try
    {
        outProgram = parser.parse();
    }
    catch (const std::exception& e)
    {
        printError(filename, source, parser.getErrorLine(), parser.getErrorColumn(), e.what());
        return false;
    }
    return true;
}

// 递归解析导入的包并合并声明到宿主程序（带环检测）
void resolveModule(ProgramNode* hostProgram, const std::string& path, const std::string& stdlibRoot,
                   std::set<std::string>& resolved, std::vector<std::string>& importStack,
                   bool& errorFlag)
{
    if (resolved.count(path)) return;

    // 环检测：import 链上再次出现同一模块
    for (const auto& s : importStack)
    {
        if (s == path)
        {
            std::cerr << "error: circular import of '" << path << "'" << std::endl;
            errorFlag = true;
            return;
        }
    }

    std::string modPath = path;
    // 完整地址（含 /，如 github.com/user/repo）保留原样；点分名 foo.bar → foo/bar
    if (modPath.find('/') == std::string::npos)
        std::replace(modPath.begin(), modPath.end(), '.', '/');
    fs::path moduleDir = fs::path(stdlibRoot) / modPath;
    if (!fs::is_directory(moduleDir))
    {
        // 标准库未命中：尝试用户包根（pvp 安装的第三方包）
        moduleDir = fs::path(plangGetPvpRoot()) / modPath;
        if (!fs::is_directory(moduleDir))
        {
            resolved.insert(path); // 模块不存在：静默忽略（保持现状）
            return;
        }
    }

    importStack.push_back(path);
    for (const auto& entry : fs::directory_iterator(moduleDir))
    {
        if (entry.path().extension() != ".plang") continue;

        std::unique_ptr<ProgramNode> libProgram;
        if (!parseSourceFile(entry.path().string(), libProgram))
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
            std::cerr << entry.path().string() << ": error: package '" << libProgram->packageName
                      << "' does not match import path '" << path << "'" << std::endl;
            errorFlag = true;
            continue;
        }
        // 先递归解析库自身的 import
        for (auto& imp : libProgram->imports)
        {
            auto* importNode = dynamic_cast<ImportStmtNode*>(imp.get());
            if (importNode)
            {
                resolveModule(hostProgram, importNode->path, stdlibRoot, resolved, importStack, errorFlag);
            }
        }
        // 库的声明与 import 并入宿主程序（合并单模块代码生成）
        for (auto& decl : libProgram->decls)
        {
            hostProgram->decls.push_back(std::move(decl));
        }
        for (auto& imp : libProgram->imports)
        {
            hostProgram->imports.push_back(std::move(imp));
        }
    }
    importStack.pop_back();
    resolved.insert(path);
}

void resolveImports(ProgramNode* program, const std::string& stdlibRoot, bool& errorFlag)
{
    std::set<std::string> resolved;
    std::vector<std::string> importStack;
    // 快照当前 import 列表（解析过程会向 program->imports 追加）
    std::vector<std::string> paths;
    for (auto& imp : program->imports)
    {
        auto* importNode = dynamic_cast<ImportStmtNode*>(imp.get());
        if (importNode) paths.push_back(importNode->path);
    }
    for (const auto& path : paths)
    {
        resolveModule(program, path, stdlibRoot, resolved, importStack, errorFlag);
    }
}

// 编译单个包（独立编译单元）：parse → 合并 → 自身 import 的 extern 注入 → 单次代码生成
bool compilePackage(const std::vector<std::string>& files,
                    const std::string& stdlibRoot, int optLevel,
                    const std::string& objPath)
{
    if (files.empty()) return false;
    std::vector<std::unique_ptr<ProgramNode>> programs;
    for (const auto& src : files)
    {
        std::unique_ptr<ProgramNode> prog;
        if (!plangParseSourceFile(src, prog)) return false;
        programs.push_back(std::move(prog));
    }
    std::unique_ptr<ProgramNode> merged = std::move(programs[0]);
    for (size_t i = 1; i < programs.size(); ++i)
    {
        for (auto& d : programs[i]->decls) merged->decls.push_back(std::move(d));
        for (auto& imp : programs[i]->imports) merged->imports.push_back(std::move(imp));
        if (merged->packageName.empty()) merged->packageName = programs[i]->packageName;
    }
    bool importError = false;
    plangResolveImports(merged.get(), stdlibRoot, importError);
    if (importError) return false;

    Sema sema;
    if (!sema.analyze(merged)) return false;
    CodeGenerator generator;
    generator.setSourceFileName(files[0]);  // DWARF 用真实源文件名
    generator.generate(merged.get(), false); // 库包无入口
    generator.optimize(optLevel);
    if (!generator.verify())
    {
        generator.saveToFile(fs::path(objPath).replace_extension(".ll").string()); // 保留 IR 供排查
        std::cerr << "package " << merged->packageName << ": IR verification failed\n";
        return false;
    }
    return generator.emitObject(objPath);
}

// 编译整个编译单元（合并 + import 解析 + 单次语义分析/代码生成）
bool compileUnit(const std::vector<std::string>& sources, bool keepIntermediate,
                 const std::string& objPath, const std::string& stdlibRoot, int optLevel,
                 std::vector<std::string>& extraObjs, bool& needSqlite)
{
    if (sources.empty()) return false;

    // 1) 解析所有源文件
    std::vector<std::unique_ptr<ProgramNode>> programs;
    std::vector<std::string> sourceTexts;
    for (const auto& src : sources)
    {
        std::ifstream file(src);
        if (!file.is_open())
        {
            std::cerr << src << ": error: cannot open file\n";
            return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();
        sourceTexts.push_back(source);

        Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        bool lexOk = true;
        for (const auto& tok : tokens)
        {
            if (tok.type == TokenType::ERROR)
            {
                printError(src, source, tok.line, tok.column, tok.text);
                lexOk = false;
            }
        }
        if (!lexOk) return false;

        Parser parser(tokens);
        try
        {
            programs.push_back(parser.parse());
        }
        catch (const std::exception& e)
        {
            printError(src, source, parser.getErrorLine(), parser.getErrorColumn(), e.what());
            return false;
        }
        // 报告该文件全部解析错误（错误恢复收集）
        for (auto& perr : parser.getErrors())
        {
            printError(src, source, perr.line, perr.column, perr.message);
        }
        if (!parser.getErrors().empty())
        {
            return false;
        }
    }

    // 1.5) 校验包名一致性：一个目录为一个包，同一编译单元的所有文件必须声明同一个包
    std::string unitPackage;
    for (size_t i = 0; i < programs.size(); ++i)
    {
        if (programs[i]->packageName.empty())
        {
            continue; // 缺少 package 声明由语义分析报错
        }
        if (unitPackage.empty())
        {
            unitPackage = programs[i]->packageName;
        }
        else if (programs[i]->packageName != unitPackage)
        {
            std::cerr << sources[i] << ": error: package '" << programs[i]->packageName
                      << "' conflicts with package '" << unitPackage
                      << "' in the same directory (one directory = one package)" << std::endl;
            return false;
        }
    }

    std::cerr << "[dbg] parse ok, files=" << programs.size() << std::endl;
    // 2) 合并：以第一个文件为主程序，其余文件声明与 import 并入
    std::unique_ptr<ProgramNode> merged = std::move(programs[0]);
    for (size_t i = 1; i < programs.size(); ++i)
    {
        for (auto& decl : programs[i]->decls)
        {
            merged->decls.push_back(std::move(decl));
        }
        for (auto& imp : programs[i]->imports)
        {
            merged->imports.push_back(std::move(imp));
        }
        if (merged->packageName.empty()) merged->packageName = programs[i]->packageName;
    }

    // 3) 解析 import：库包函数注入 extern 声明、类型合并；记录包路径供独立编译
    bool importError = false;
    std::vector<std::string> packages;
    plangResolveImports(merged.get(), stdlibRoot, importError, &packages);
    if (importError) return false;
    // 需要外部链接库：std.sqlite → -lsqlite3
    for (const auto& pkg : packages)
    {
        if (pkg == "std.sqlite") needSqlite = true;
    }

    std::cerr << "[dbg] resolve ok, decls=" << merged->decls.size() << std::endl;
    // 4) 语义分析
    Sema sema;
    bool ok = sema.analyze(merged);
    std::cerr << "[dbg] sema done, errors=" << sema.getErrors().size() << std::endl;
    for (const auto& w : sema.getWarnings())
    {
        printWarning(sources[0], sourceTexts[0], w.line, w.column, w.message);
    }
    if (!ok)
    {
        for (const auto& err : sema.getErrors())
        {
            printError(sources[0], sourceTexts[0], err.line, err.column, err.message);
        }
        return false;
    }

    // 5) 代码生成
    CodeGenerator generator;
    generator.setSourceFileName(sources[0]);  // DWARF 用真实源文件名
    generator.generate(merged.get());
    generator.optimize(optLevel);

    if (!generator.verify())
    {
        std::string llPath = withExtension(sources[0], ".ll");
        generator.saveToFile(llPath);
        std::cerr << sources[0] << ": error: IR verification failed (saved to " << llPath << ")\n";
        return false;
    }

    // 可选保留 .ll
    if (keepIntermediate)
    {
        std::string llPath = withExtension(sources[0], ".ll");
        generator.saveToFile(llPath);
    }

    // 可选保留 .ll
    if (keepIntermediate)
    {
        std::string llPath = withExtension(sources[0], ".ll");
        generator.saveToFile(llPath);
    }

    if (!generator.emitObject(objPath))
    {
        std::cerr << sources[0] << ": error: object generation failed\n";
        return false;
    }

    // 4) 独立编译每个导入的库包为 .o（与主模块链接）
    for (const auto& pkg : packages)
    {
        std::string pkgPath = pkg;
        if (pkgPath.find('/') == std::string::npos)
            std::replace(pkgPath.begin(), pkgPath.end(), '.', '/');
        fs::path pkgDir = fs::path(stdlibRoot) / pkgPath;
        if (!fs::is_directory(pkgDir))
        {
            // 第三方包：pvp 用户包根
            pkgDir = fs::path(plangGetPvpRoot()) / pkgPath;
        }
        if (!fs::is_directory(pkgDir)) continue;
        std::vector<std::string> pkgFiles;
        for (const auto& entry : fs::directory_iterator(pkgDir))
        {
            if (entry.path().extension() == ".plang") pkgFiles.push_back(entry.path().string());
        }
        if (pkgFiles.empty()) continue;

        std::string safeName = pkg;
        std::replace(safeName.begin(), safeName.end(), '.', '_');
        std::replace(safeName.begin(), safeName.end(), '/', '_');
        std::string pkgObj = "plangc_lib_" + safeName + ".o";
        std::cout << "compiling package " << pkg << " -> " << pkgObj << std::endl;
        if (!compilePackage(pkgFiles, stdlibRoot, optLevel, pkgObj))
        {
            return false;
        }
        extraObjs.push_back(pkgObj);
    }

    return true;
}

// 打包静态库
bool buildStaticLibrary(const std::vector<std::string>& objFiles, 
                        const std::string& outputName) {
    if (objFiles.empty()) {
        std::cerr << "error: no object files to archive\n";
        return false;
    }
    
    std::string libName = outputName;
    // 自动添加 lib 前缀（如果没有）
    fs::path p(libName);
    if (p.filename().string().find("lib") != 0 && p.filename().string().find("lib") == std::string::npos) {
        libName = (p.parent_path() / ("lib" + p.filename().string())).string();
    }
    // 确保 .a 后缀
    if (p.extension() != ".a") {
        libName += ".a";
    }
    
    std::string cmd = "ar rcs " + libName;
    for (const auto& obj : objFiles) {
        cmd += " " + obj;
    }
    
    std::cout << "Creating static library: " << libName << std::endl;
    return std::system(cmd.c_str()) == 0;
}

// ============ pLangLists.json 构建管理系统 ============
// 类似 CMakeLists.txt：项目清单定义入口/源文件/输出/优化/链接库，
// plc build / run / clean / init 驱动构建。

struct Manifest
{
    std::string name;
    std::string version;
    std::string kind = "executable";     // executable | library
    std::string entry;                   // 入口源文件（executable）
    std::vector<std::string> sources;    // 额外源文件
    std::string output;                  // 输出文件名（默认 name）
    int optimization = 2;
    std::vector<std::string> linkLibraries;
    std::vector<std::string> imports;    // 第三方 import 根（后续启用）
};

// 读取并解析 pLangLists.json（目录内），成功返回 true
bool parseManifest(const std::string& dir, Manifest& m)
{
    fs::path manifestPath = fs::path(dir) / "pLangLists.json";
    std::ifstream file(manifestPath);
    if (!file.is_open()) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto parsed = llvm::json::parse(buffer.str());
    if (!parsed) return false;
    auto* obj = parsed->getAsObject();
    if (!obj) return false;

    if (auto s = obj->getString("name")) m.name = s->str();
    if (auto s = obj->getString("version")) m.version = s->str();
    if (auto s = obj->getString("kind")) m.kind = s->str();
    if (auto s = obj->getString("entry")) m.entry = s->str();
    if (auto s = obj->getString("output")) m.output = s->str();
    if (auto i = obj->getInteger("optimization")) m.optimization = (int)*i;
    if (auto arr = obj->getArray("sources"))
    {
        for (auto& e : *arr)
            if (auto s = e.getAsString()) m.sources.push_back(s->str());
    }
    if (auto lk = obj->getObject("link"))
    {
        if (auto arr = lk->getArray("libraries"))
            for (auto& e : *arr)
                if (auto s = e.getAsString()) m.linkLibraries.push_back(s->str());
    }
    if (auto arr = obj->getArray("import"))
    {
        for (auto& e : *arr)
            if (auto s = e.getAsString()) m.imports.push_back(s->str());
    }
    return true;
}

// 输出文件名（无则默认 name）
static std::string manifestOutput(const Manifest& m)
{
    return m.output.empty() ? (m.name.empty() ? "output" : m.name) : m.output;
}

// 收集源文件：entry + sources；未指定则收集目录内全部 .plang
static std::vector<std::string> manifestSources(const Manifest& m, const std::string& dir)
{
    std::vector<std::string> out;
    std::vector<std::string> explicitList;
    if (!m.entry.empty()) explicitList.push_back(m.entry);
    for (auto& s : m.sources) explicitList.push_back(s);
    for (auto& s : explicitList)
    {
        fs::path p = s;
        if (!p.is_absolute()) p = fs::path(dir) / p;
        if (fs::exists(p)) out.push_back(p.string());
        else std::cerr << "warning: source not found: " << p.string() << "\n";
    }
    if (out.empty())
    {
        // 自动收集目录内 .plang
        for (const auto& entry : fs::directory_iterator(dir))
            if (entry.path().extension() == ".plang") out.push_back(entry.path().string());
    }
    return out;
}

// 打印构建概要
static void printManifestSummary(const Manifest& m, const std::string& dir)
{
    std::cout << "pLangLists.json: " << fs::path(dir).filename().string() << "/pLangLists.json\n";
    std::cout << "  name = " << (m.name.empty() ? "(unnamed)" : m.name)
              << "  kind = " << m.kind
              << "  optimization = -O" << m.optimization << "\n";
    if (!m.version.empty()) std::cout << "  version = " << m.version << "\n";
    if (!m.entry.empty()) std::cout << "  entry = " << m.entry << "\n";
}

// plc build / plc run：按清单编译并链接
int cmdBuild(const std::string& dir, bool runAfter, const std::vector<std::string>& runArgs,
             const char* argv0)
{
    Manifest m;
    if (!parseManifest(dir, m))
    {
        std::cerr << "error: no pLangLists.json in '" << dir << "' (run 'plc init <name>' first)\n";
        return 1;
    }
    printManifestSummary(m, dir);

    std::vector<std::string> sources = manifestSources(m, dir);
    if (sources.empty())
    {
        std::cerr << "error: no .plang sources (set entry/sources or add .plang files)\n";
        return 1;
    }

    std::string stdlibRoot = getStdlibRoot(argv0);
    std::string obj = "plangc_tmp_0.o";
    std::vector<std::string> extraObjs;
    bool needSqlite = false;
    if (!compileUnit(sources, false, obj, stdlibRoot, m.optimization, extraObjs, needSqlite))
        return 1;
    std::vector<std::string> objFiles = { obj };
    for (auto& eo : extraObjs) objFiles.push_back(eo);

    int result = 1;
    if (m.kind == "library")
    {
        std::string libName = manifestOutput(m);
        std::cout << "archiving " << objFiles.size() << " object(s) -> " << libName << "\n";
        result = buildStaticLibrary(objFiles, libName) ? 0 : 1;
    }
    else
    {
        std::string exeName = fs::path(dir) / manifestOutput(m);
        std::cout << "linking -> " << exeName << "\n";
        result = linkExecutable(objFiles, exeName, needSqlite, m.linkLibraries) ? 0 : 1;
        if (result == 0 && runAfter)
        {
            std::string cmd = "\"" + exeName + "\"";
            for (auto& a : runArgs) cmd += " \"" + a + "\"";
            std::cout << "running: " << exeName << "\n";
            result = (std::system(cmd.c_str()) == 0) ? 0 : 1;
        }
    }

    // 清理临时 .o
    for (const auto& o : objFiles) fs::remove(o);
    return result;
}

// plc clean：清理构建产物（可执行/静态库）
int cmdClean(const std::string& dir)
{
    Manifest m;
    if (!parseManifest(dir, m))
    {
        std::cerr << "error: no pLangLists.json in '" << dir << "'\n";
        return 1;
    }
    std::string out = fs::path(dir) / manifestOutput(m);
    int removed = 0;
    for (const auto& candidate : { out, out + ".a", out + ".dSYM" })
    {
        if (fs::exists(candidate))
        {
            fs::remove_all(candidate);
            std::cout << "removed " << candidate << "\n";
            ++removed;
        }
    }
    if (removed == 0) std::cout << "nothing to clean\n";
    return 0;
}

// plc init：生成 pLangLists.json 模板
int cmdInit(const std::string& name, const std::string& dir)
{
    if (name.empty())
    {
        std::cerr << "usage: plc init <name>\n";
        return 1;
    }
    fs::path manifestPath = fs::path(dir) / "pLangLists.json";
    if (fs::exists(manifestPath))
    {
        std::cerr << "error: " << manifestPath.string() << " already exists\n";
        return 1;
    }
    std::string kind = "executable";
    std::string entry = name + ".plang";
    if (!fs::exists(fs::path(dir) / entry))
    {
        // 同时生成一个最小入口文件
        std::ofstream f(fs::path(dir) / entry);
        f << "package " << name << ";\n"
          << "import std.io;\n\n"
          << "func main() : int {\n"
          << "    io.println(\"hello from " << name << "\");\n"
          << "    return 0;\n"
          << "}\n";
        f.close();
        std::cout << "created " << entry << "\n";
    }
    std::ofstream f(manifestPath);
    f << "{\n"
      << "  \"name\": \"" << name << "\",\n"
      << "  \"version\": \"0.1.0\",\n"
      << "  \"kind\": \"" << kind << "\",\n"
      << "  \"entry\": \"" << entry << "\",\n"
      << "  \"sources\": [],\n"
      << "  \"optimization\": 2,\n"
      << "  \"link\": { \"libraries\": [] },\n"
      << "  \"dependencies\": [],\n"
      << "  \"import\": []\n"
      << "}\n";
    f.close();
    std::cout << "created " << manifestPath.string() << "\n";
    std::cout << "now run: plc build\n";
    return 0;
}

// main 函数
int main(int argc, char* argv[]) {
    bool compileOnly = false;
    bool buildStatic = false;
    bool keepIntermediate = false;
    int optLevel = 2;   // 默认 O2 优化
    std::string outputName;
    std::vector<std::string> inputFiles;

    // pLangLists.json 子命令模式：plc build [dir] / run [dir] [args] / clean [dir] / init <name>
    if (argc >= 2 && std::string(argv[1]) == "build")
    {
        std::string dir = (argc >= 3 && std::string(argv[2]).front() != '-') ? argv[2] : ".";
        return cmdBuild(dir, false, {}, argv[0]);
    }
    if (argc >= 2 && std::string(argv[1]) == "run")
    {
        std::vector<std::string> runArgs;
        std::string dir = ".";
        int i = 2;
        if (i < argc && std::string(argv[i]).front() != '-') dir = argv[i++];
        for (; i < argc; ++i) runArgs.push_back(argv[i]);
        return cmdBuild(dir, true, runArgs, argv[0]);
    }
    if (argc >= 2 && std::string(argv[1]) == "clean")
    {
        std::string dir = (argc >= 3 && std::string(argv[2]).front() != '-') ? argv[2] : ".";
        return cmdClean(dir);
    }
    if (argc >= 2 && std::string(argv[1]) == "init")
    {
        std::string name = (argc >= 3) ? argv[2] : "";
        std::string dir = (argc >= 4) ? argv[3] : ".";
        return cmdInit(name, dir);
    }
    
    // 解析参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.size() == 3 && arg[0] == '-' && arg[1] == 'O') {
            optLevel = arg[2] - '0';
        } else if (arg == "-c") {
            compileOnly = true;
        } else if (arg == "-static") {
            buildStatic = true;
        } else if (arg == "--save-temps") {
            keepIntermediate = true;
        } else if (arg == "-o" && i + 1 < argc) {
            outputName = argv[++i];
        } else if (arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            return 1;
        } else {
            inputFiles.push_back(arg);
        }
    }
    
    if (inputFiles.empty()) {
        std::cerr << "usage: plc [options] <file.plang | directory>\n";
        std::cerr << "options:\n";
        std::cerr << "  -c              compile to object file only (.o)\n";
        std::cerr << "  -static         build static library (.a)\n";
        std::cerr << "  -o <file>       output file name (default: source name without .plang)\n";
        std::cerr << "  --save-temps    keep intermediate files (.ll, .o)\n";
        std::cerr << "\nproject mode (pLangLists.json):\n";
        std::cerr << "  plc build [dir]     compile+link per pLangLists.json\n";
        std::cerr << "  plc run [dir] [args]  build then run\n";
        std::cerr << "  plc clean [dir]     remove build artifacts\n";
        std::cerr << "  plc init <name>     generate pLangLists.json + entry file\n";
        return 1;
    }
    
    // 收集所有 .plang 文件
    std::vector<std::string> sources = collectPlangFiles(inputFiles);
    if (sources.empty()) {
        std::cerr << "error: no .plang files found\n";
        return 1;
    }
    
    // 阶段1：合并编译所有源文件为单个 .o（含 import 解析）
    std::string obj;
    if (compileOnly && !outputName.empty()) {
        obj = outputName;
    } else if (compileOnly && sources.size() == 1) {
        obj = withExtension(sources[0], ".o");
    } else {
        obj = "plangc_tmp_0.o";
    }

    std::cout << "compiling " << sources.size() << " file(s) -> " << obj << std::endl;
    std::string stdlibRoot = getStdlibRoot(argv[0]);
    std::vector<std::string> extraObjs;
    bool needSqlite = false;
    if (!compileUnit(sources, keepIntermediate, obj, stdlibRoot, optLevel, extraObjs, needSqlite)) {
        return 1;
    }
    std::vector<std::string> objFiles = { obj };
    for (auto& eo : extraObjs) objFiles.push_back(eo);
    
    // 阶段2：根据模式处理
    if (compileOnly) {
        // 只生成 .o，不做后续处理
        std::cout << "object files generated: " << objFiles.size() << std::endl;
        return 0;
    } else if (buildStatic) {
        // 打包静态库
        std::string libName = outputName.empty() ? "liboutput.a" : outputName;
        bool success = buildStaticLibrary(objFiles, libName);
        
        // 清理临时 .o（除非 --save-temps）
        if (!keepIntermediate) {
            for (const auto& obj : objFiles) {
                fs::remove(obj);
            }
        }
        return success ? 0 : 1;
    } else {
        // 默认：链接成可执行文件（默认输出名 = 第一个源文件去扩展名）
        std::string exeName = outputName;
        if (exeName.empty())
        {
            // 输出到第一个源文件所在目录（main.plang → main）
            const std::string& first = sources[0];
            exeName = fs::path(first).parent_path().empty()
                ? fs::path(first).stem().string()
                : (fs::path(first).parent_path() / fs::path(first).stem()).string();
        }
        bool success = linkExecutable(objFiles, exeName, needSqlite);
        
        // 清理临时 .o（除非 --save-temps）
        if (!keepIntermediate) {
            for (const auto& obj : objFiles) {
                fs::remove(obj);
            }
        }
        return success ? 0 : 1;
    }
}