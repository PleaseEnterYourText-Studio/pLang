#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include "Lexer.h"
#include "Parser.h"
#include "Sema.h"
#include "CodeGenerator.h"
#include "AST.h"
#include "token.h"

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

int main (int argc, char* argv[]) {
	if (argc < 2) {
		std::cerr << "usage: PLang <source.plang>\n";
		return 1;
	}

	std::string filename = argv[1];
	std::ifstream file (filename);
	if (!file.is_open ()) {
		std::cerr << filename << ": error: cannot open file\n";
		return 1;
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
		return 1;

	// 语法分析
	Parser parser (tokens);
	std::unique_ptr<ProgramNode> program;
	try {
		program = parser.parse ();
	} catch (const std::exception& e) {
		// Parser 的异常信息不含行列，需要从最近的错误位置补
		printError (filename, source, parser.getErrorLine (), parser.getErrorColumn (), e.what ());
		return 1;
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
		return 1;
	}

	std::cout << "analysis passed\n";

	// LLVM IR 代码生成
	std::cout << "Generating LLVM IR...\n";
	CodeGenerator generator;
	generator.generate (program.get ());

	if (!generator.verify ()) {
		std::cerr << "IR verification failed!\n";
		return 1;
	}

	std::cout << "Generated LLVM IR:\n";
	generator.printIR ();
	std::cout << "\n";

	generator.saveToFile ("output.ll");
	std::cout << "IR saved to output.ll\n";

	if (!generator.emitObject ("output.o")) {
		std::cerr << "object generation failed\n";
		return 1;
	}
	std::cout << "object saved to output.o\n";

#if defined(__APPLE__)
	int linkResult = std::system ("ld -o output output.o -lSystem -syslibroot $(xcrun --show-sdk-path) -e _main "
	                              "2>/dev/null || cc output.o -o output");
#elif defined(__linux__)
	int linkResult = std::system ("ld -o output output.o");
#else
	int linkResult = 1;
#endif
	if (linkResult != 0) {
		std::cerr << "link failed\n";
		return 1;
	}
	std::cout << "executable saved to output\n";

	return 0;
}
