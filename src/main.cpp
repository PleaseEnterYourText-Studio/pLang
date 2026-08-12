#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
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
    // Linux 需要 crt 文件，用 gcc 驱动更简单
    cmd = "g++ -o " + exePath;
    for (const auto& obj : objFiles) cmd += " " + obj;
#else
    std::cerr << "error: unsupported platform for linking\n";
    return false;
#endif
    
    return std::system(cmd.c_str()) == 0;
}

// ===== 编译单个源文件为 .o =====
bool compileToObject(const std::string& filename, bool keepIntermediate, 
                     const std::string& objPath) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << filename << ": error: cannot open file\n";
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    // 词法分析
    Lexer lexer(source);
    auto tokens = lexer.scanTokens();
    int lexErrors = 0;
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::ERROR) {
            printError(filename, source, tok.line, tok.column, tok.text);
            lexErrors++;
        }
    }
    if (lexErrors > 0) return false;

    // 语法分析
    Parser parser(tokens);
    std::unique_ptr<ProgramNode> program;
    try {
        program = parser.parse();
    } catch (const std::exception& e) {
        printError(filename, source, parser.getErrorLine(), parser.getErrorColumn(), e.what());
        return false;
    }

    // 语义分析
    Sema sema;
    bool ok = sema.analyze(program);
    for (const auto& w : sema.getWarnings()) {
        printWarning(filename, source, w.line, w.column, w.message);
    }
    if (!ok) {
        for (const auto& err : sema.getErrors()) {
            printError(filename, source, err.line, err.column, err.message);
        }
        return false;
    }

    // 代码生成
    CodeGenerator generator;
    generator.generate(program.get());

    if (!generator.verify()) {
        std::cerr << filename << ": error: IR verification failed\n";
        return false;
    }

    // 可选保留 .ll
    if (keepIntermediate) {
        std::string llPath = withExtension(filename, ".ll");
        generator.saveToFile(llPath);
    }

    if (!generator.emitObject(objPath)) {
        std::cerr << filename << ": error: object generation failed\n";
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

// ===== 旧版 compileFile（保留兼容） =====
bool compileFile(const std::string& filename, bool keepIntermediate) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << filename << ": error: cannot open file\n";
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    // ...（词法、语法、语义分析同上，省略重复代码）...
    // 为了简洁，这里直接调用 compileToObject + 链接
    std::string objPath = withExtension(filename, ".o");
    if (!compileToObject(filename, keepIntermediate, objPath)) {
        return false;
    }
    
    std::string exePath = withExtension(filename, "");
    return linkExecutable({objPath}, exePath);
}

// ===== main 函数 =====
int main(int argc, char* argv[]) {
    bool compileOnly = false;
    bool buildStatic = false;
    bool keepIntermediate = false;
    std::string outputName;
    std::vector<std::string> inputFiles;
    
    // 解析参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c") {
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
    
    // 阶段1：编译所有源文件为 .o
    std::vector<std::string> objFiles;
    int index = 0;
    for (const auto& src : sources) {
        // 确定 .o 路径
        std::string obj;
        if (sources.size() == 1 && outputName.empty()) {
            // 单个文件，用源文件同名
            obj = withExtension(src, ".o");
        } else if (!outputName.empty() && !compileOnly && !buildStatic) {
            // 指定了 -o 且链接成可执行文件，用临时 .o
            obj = "plangc_tmp_" + std::to_string(index++) + ".o";
        } else {
            // 默认：源文件同名 .o
            obj = withExtension(src, ".o");
        }
        
        std::cout << "compiling " << src << " -> " << obj << std::endl;
        if (!compileToObject(src, keepIntermediate, obj)) {
            return 1;
        }
        objFiles.push_back(obj);
    }
    
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