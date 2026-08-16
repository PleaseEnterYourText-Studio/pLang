#ifndef IMPORTER_H
#define IMPORTER_H

#include <string>
#include <vector>
#include <set>
#include <memory>
#include "AST.h"

// 跨文件 import 解析（plangc 与 LSP 共用）

// 解析单个源文件（词法+语法），出错返回 false
bool plangParseSourceFile(const std::string& filename, std::unique_ptr<ProgramNode>& outProgram);

// 标准库根目录：import std.thread 对应 <root>/std/thread
std::string plangGetStdlibRoot(const std::string& exePath);

// 解析 import：把标准库包源码合并进宿主程序（带环检测）
void plangResolveImports(ProgramNode* hostProgram, const std::string& stdlibRoot, bool& errorFlag);

#endif
