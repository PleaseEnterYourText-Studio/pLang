#ifndef CODEGENERATOR_H
#define CODEGENERATOR_H

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"
#include "AST.h"

class CodeGenerator
{
private:
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;
    llvm::IRBuilder<> builder;

    llvm::Function* currentFunction;

    // 变量指针 + 其类型
    struct VarInfo
    {
        llvm::AllocaInst* ptr;
        llvm::Type* type;
    };
    std::unordered_map<std::string, VarInfo> namedValues;
    std::unordered_map<std::string, llvm::Type*> namedValueElementTypes;  // 指针变量/参数 → 指向类型
    std::unordered_map<std::string, llvm::GlobalVariable*> externGlobals;  // extern 全局数据

    // std.thread 内置支持
    int mutexSlotCount = 0;             // 已分配的互斥锁槽位数（池上限 64）
    int threadTrampolineCounter = 0;    // 线程入口蹦床函数计数器

    llvm::Type* getLLVMType(ASTNodeType type);
    llvm::Type* getLLVMType(TypeNode* type);
    llvm::Type* getLLVMType(const std::string& typeName);

    llvm::Value* getVariable(const std::string& name);
    llvm::Value* generateExpression(ASTNode* node);
    void generateStatement(ASTNode* node);
    void generateFunction(FunctionDeclNode* fn);

    // 数组/指针下标 buf[i]：计算元素地址并返回元素类型
    llvm::Value* getIndexedAddress(IndexNode* node, llvm::Type*& elemType);

    // std.thread 内置调用生成
    llvm::Value* generateThreadBuiltin(FunctionCallNode* call);
    llvm::Function* getOrDeclareFunction(const std::string& name,
                                         llvm::Type* retType,
                                         const std::vector<llvm::Type*>& paramTypes);
    llvm::GlobalVariable* getMutexPool();
    llvm::Value* getMutexSlotAddress(llvm::Value* slotValue);

    // 统一二元/比较操作数类型（整数拓宽、整数↔浮点提升），返回公共类型
    llvm::Type* unifyOperands(llvm::Value*& left, llvm::Value*& right);

public:
    CodeGenerator();
    ~CodeGenerator() = default;

    void generate(ProgramNode* root);
    void printIR();
    void saveToFile(const std::string& filename);
    bool verify();
    bool emitObject(const std::string& filename);
};

#endif
