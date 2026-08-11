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

// 从源码中提取第 line 行的内容
std::string getLine (const std::string& source, int line) {
	std::istringstream stream (source);
	std::string lineText;
	for (int i = 1; i <= line; ++i) {
		if (!std::getline (stream, lineText)) {
			return "";
		}
	}
	return lineText;
}

void printError (const std::string& filename, const std::string& source, int line, int column,
                 const std::string& message) {
	std::cerr << filename << ":" << line << ":" << column << ":\n";
	std::cerr << "  error: " << message << "\n";
	std::string lineText = getLine (source, line);
	if (!lineText.empty ()) {
		std::cerr << "  " << line << " | " << lineText << "\n";
		std::cerr << "  " << std::string (std::to_string (line).size (), ' ') << " | " << std::string (column - 1, ' ')
		          << "^" << "\n";
	}
}

void printWarning (const std::string& filename, const std::string& source, int line, int column,
                   const std::string& message) {
	std::cerr << filename << ":" << line << ":" << column << ":\n";
	std::cerr << "  warning: " << message << "\n";
	std::string lineText = getLine (source, line);
	if (!lineText.empty ()) {
		std::cerr << "  " << line << " | " << lineText << "\n";
		std::cerr << "  " << std::string (std::to_string (line).size (), ' ') << " | " << std::string (column - 1, ' ')
		          << "^" << "\n";
	}
}

// 替换扩展名为新后缀: "dir/foo.plang" + ".o" -> "dir/foo.o"
std::string withExtension (const std::string& path, const std::string& newExt) {
	fs::path p (path);
	return p.replace_extension (newExt).string ();
}

// 编译单个 .plang 文件，产物为同名可执行文件
// keepIntermediate 为 true 时保留 .ll/.o 中间产物
bool compileFile (const std::string& filename, bool keepIntermediate) {
	std::ifstream file (filename);
	if (!file.is_open ()) {
		std::cerr << filename << ": error: cannot open file\n";
		return false;
	}

	std::stringstream buffer;
	buffer << file.rdbuf ();
	std::string source = buffer.str ();

	// 词法分析
	Lexer lexer (source);
	auto tokens = lexer.scanTokens ();
	int lexErrors = 0;
	for (const auto& tok : tokens) {
		if (tok.type == TokenType::ERROR) {
			printError (filename, source, tok.line, tok.column, tok.text);
			lexErrors++;
		}
	}
	if (lexErrors > 0)
		return false;

	// 语法分析
	Parser parser (tokens);
	std::unique_ptr<ProgramNode> program;
	try {
		program = parser.parse ();
	} catch (const std::exception& e) {
		printError (filename, source, parser.getErrorLine (), parser.getErrorColumn (), e.what ());
		return false;
	}

	// 语义分析
	Sema sema;
	bool ok = sema.analyze (program);
	for (const auto& w : sema.getWarnings ()) {
		printWarning (filename, source, w.line, w.column, w.message);
	}
	if (!ok) {
		for (const auto& err : sema.getErrors ()) {
			printError (filename, source, err.line, err.column, err.message);
		}
		return false;
	}

	// LLVM IR 代码生成
	CodeGenerator generator;
	generator.generate (program.get ());

	if (!generator.verify ()) {
		std::cerr << filename << ": error: IR verification failed\n";
		return false;
	}

	std::string objPath = withExtension (filename, ".o");
	std::string exePath = withExtension (filename, "");
	std::string llPath = withExtension (filename, ".ll");

	// 不保留中间产物时，用临时文件并在链接后清理
	if (!keepIntermediate) {
		objPath = "plangc_tmp.o";
		llPath = "plangc_tmp.ll";
	}

	generator.saveToFile (llPath);

	if (!generator.emitObject (objPath)) {
		std::cerr << filename << ": error: object generation failed\n";
		if (!keepIntermediate) {
			fs::remove (objPath);
			fs::remove (llPath);
		}
		return false;
	}

	// 链接（平台相关）
	int linkResult;
#if defined(__APPLE__)
	linkResult = std::system (("ld -o " + exePath + " " + objPath +
	                          " -lSystem -syslibroot $(xcrun --show-sdk-path) -e _main").c_str ());
#elif defined(__linux__)
	linkResult = std::system (("ld -o " + exePath + " " + objPath).c_str ());
#else
	linkResult = 1;
#endif
	if (linkResult != 0) {
		std::cerr << filename << ": error: link failed\n";
	}

	if (!keepIntermediate) {
		fs::remove (objPath);
		fs::remove (llPath);
	}

	return linkResult == 0;
}

int main (int argc, char* argv[]) {
	if (argc < 2) {
		std::cerr << "usage: plangc [options] <file.plang | directory>\n";
		std::cerr << "options:\n";
		std::cerr << "  -k    keep intermediate files (.ll, .o)\n";
		return 1;
	}

	bool keepIntermediate = false;
	std::string target;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-k") {
			keepIntermediate = true;
		} else if (!arg.empty () && arg[0] == '-' && arg != ".") {
			std::cerr << "unknown option: " << arg << "\n";
			return 1;
		} else {
			target = arg;
		}
	}

	if (target.empty ()) {
		std::cerr << "no input file\n";
		return 1;
	}

	// plangc . —— 编译目录下所有 .plang
	if (target == "." || fs::is_directory (target)) {
		std::vector<std::string> files;
		for (const auto& entry : fs::directory_iterator (target)) {
			if (entry.path ().extension () == ".plang") {
				files.push_back (entry.path ().string ());
			}
		}
		if (files.empty ()) {
			std::cerr << "no .plang files found in " << target << "\n";
			return 1;
		}
		int failed = 0;
		for (const auto& f : files) {
			std::cout << "compiling " << f << "\n";
			if (!compileFile (f, keepIntermediate)) {
				failed++;
			}
		}
		std::cout << "done: " << (files.size () - failed) << " succeeded, " << failed << " failed\n";
		return failed == 0 ? 0 : 1;
	}

	// plangc file.plang —— 编译单个文件
	return compileFile (target, keepIntermediate) ? 0 : 1;
}
