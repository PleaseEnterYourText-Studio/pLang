#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <algorithm>
#include <set>
#include <filesystem>
#include "Lexer.h"
#include "Parser.h"
#include "Sema.h"
#include "CodeGenerator.h"
#include "AST.h"
#include "token.h"

namespace fs = std::filesystem;

// ===== 工具函数（保持不变） =====
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

// ===== 收集 .plang 文件 =====
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

// ===== 链接可执行文件 =====
bool linkExecutable(const std::vector<std::string>& objFiles, const std::string& exePath) {
    if (objFiles.empty()) {
        std::cerr << "error: no object files to link\n";
        return false;
    }
    
    std::string cmd;
#if defined(__APPLE__)
    cmd = "ld -o " + exePath;
    for (const auto& obj : objFiles) cmd += " " + obj;
    cmd += " -lSystem -syslibroot $(xcrun --show-sdk-path) -e _main";
#elif defined(__linux__)
    cmd = "ld -o " + exePath;
    for (const auto& obj : objFiles) cmd += " " + obj;
    // Linux 需要 crt 文件，用 gcc 驱动更简单；多线程需要链接 pthread
    cmd = "g++ -o " + exePath;
    for (const auto& obj : objFiles) cmd += " " + obj;
    cmd += " -pthread";
#elif defined(_WIN32)
    // Windows：用 clang 驱动（自动处理 CRT 与入口），对象文件 .obj
    cmd = "clang++ -o " + exePath;
    for (const auto& obj : objFiles) cmd += " " + obj;
#else
    std::cerr << "error: unsupported platform for linking\n";
    return false;
#endif
    
    return std::system(cmd.c_str()) == 0;
}

// 标准库根目录
// import std.thread 对应目录 <root>/std/thread，故 root 为包层级根（仓库根）。
std::string getStdlibRoot(const std::string& exePath)
{
    if (const char* env = std::getenv("PLANG_STD"))
    {
        return env;
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
    std::replace(modPath.begin(), modPath.end(), '.', '/');
    fs::path moduleDir = fs::path(stdlibRoot) / modPath;
    if (!fs::is_directory(moduleDir))
    {
        resolved.insert(path); // 模块不存在：静默忽略（保持现状）
        return;
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
        if (libProgram->packageName != path)
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

// 编译整个编译单元（合并 + import 解析 + 单次语义分析/代码生成）
bool compileUnit(const std::vector<std::string>& sources, bool keepIntermediate,
                 const std::string& objPath, const std::string& stdlibRoot, int optLevel)
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

    // 3) 解析 import：加载标准库包源码并合并
    bool importError = false;
    resolveImports(merged.get(), stdlibRoot, importError);
    if (importError) return false;

    // 4) 语义分析
    Sema sema;
    bool ok = sema.analyze(merged);
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
    generator.generate(merged.get());
    generator.optimize(optLevel);

    if (!generator.verify())
    {
        std::cerr << sources[0] << ": error: IR verification failed\n";
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

    return true;
}

// ===== 打包静态库 =====
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

// ===== main 函数 =====
int main(int argc, char* argv[]) {
    bool compileOnly = false;
    bool buildStatic = false;
    bool keepIntermediate = false;
    int optLevel = 2;   // 默认 O2 优化
    std::string outputName;
    std::vector<std::string> inputFiles;
    
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
        std::cerr << "usage: plangc [options] <file.plang | directory>\n";
        std::cerr << "options:\n";
        std::cerr << "  -c              compile to object file only (.o)\n";
        std::cerr << "  -static         build static library (.a)\n";
        std::cerr << "  -o <file>       output file name\n";
        std::cerr << "  --save-temps    keep intermediate files (.ll, .o)\n";
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
    if (!compileUnit(sources, keepIntermediate, obj, stdlibRoot, optLevel)) {
        return 1;
    }
    std::vector<std::string> objFiles = { obj };
    
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
        // 默认：链接成可执行文件
        std::string exeName = outputName.empty() ? "a.out" : outputName;
        bool success = linkExecutable(objFiles, exeName);
        
        // 清理临时 .o（除非 --save-temps）
        if (!keepIntermediate) {
            for (const auto& obj : objFiles) {
                fs::remove(obj);
            }
        }
        return success ? 0 : 1;
    }
}