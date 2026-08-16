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

// 解析 import：库包函数注入 extern 声明、结构体/类型合并进宿主程序（带环检测）。
// resolvedPackages（可选）：实际解析到的包路径（供独立编译库包使用）。
void plangResolveImports(ProgramNode* hostProgram, const std::string& stdlibRoot, bool& errorFlag,
                         std::vector<std::string>* resolvedPackages = nullptr);

// 深拷贝 TypeNode（注入声明用）
std::unique_ptr<TypeNode> plangCloneType(TypeNode* t);

#endif
