#ifndef CODEGENERATOR_H
#define CODEGENERATOR_H

#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <memory>
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DIBuilder.h"
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
    std::unordered_map<std::string, llvm::Type*> fieldPointeeTypes;       // "结构体.指针字段" → 指向类型
    std::unordered_map<std::string, llvm::GlobalVariable*> externGlobals;  // extern 全局数据
    std::set<std::string> volatileVars;   // volatile 变量（访问走 volatile load/store）

    // std.thread 内置支持
    int mutexSlotCount = 0;             // 已分配的互斥锁槽位数（池上限 64）
    int threadTrampolineCounter = 0;    // 线程入口蹦床函数计数器

    llvm::Type* getLLVMType(ASTNodeType type);
    llvm::Type* getLLVMType(TypeNode* type);
    llvm::Type* getLLVMType(const std::string& typeName);

    llvm::Value* getVariable(const std::string& name);
    llvm::Value* generateExpression(ASTNode* node);
    void generateStatement(ASTNode* node);
    void generateFunction(FunctionDeclNode* fn, bool isMethod = false,
                           const std::string& structName = "");

    // 数组/指针下标 buf[i]：计算元素地址并返回元素类型
    llvm::Value* getIndexedAddress(IndexNode* node, llvm::Type*& elemType);

    // 指针操作数的元素类型（变量已知指向类型时返回之，否则按 i8 字节寻址）
    llvm::Type* pointerElementType(ASTNode* operandNode);

    // 调用点参数类型转换（C 式隐式转换）
    llvm::Value* coerceValue(llvm::Value* val, llvm::Type* targetTy);

    // 结构体类型注册表：名字 → LLVM 结构体类型 + 字段信息（按声明顺序）
    struct StructDef
    {
        llvm::StructType* type;
        std::vector<std::string> fieldNames;
        std::vector<llvm::Type*> fieldTypes;
        std::vector<int> fieldBits;     // 位域宽度（0=普通字段）
        bool isUnion = false;           // union：所有字段共享同一内存
        int alignBytes = 0;             // 对齐（0=默认）
        bool hasConstruction = false;   // .construction() 自动构造
        bool hasDestruction = false;    // .destroy() 自动析构
    };
    std::unordered_map<std::string, StructDef> structDefs;

    // 取结构体字段地址（GEP/bitcast），并输出字段类型与位宽
    llvm::Value* getStructFieldPtr(llvm::Value* structPtr, const std::string& structName,
                                   const std::string& fieldName, llvm::Type*& fieldType, int& bitWidth);
    // 多级成员地址解析 a.b.c：逐级解析，输出最终字段类型与位宽
    llvm::Value* getMemberAddress(const std::string& dottedName, llvm::Type*& fieldType, int& bitWidth);
    llvm::Type* getPointeeType(const std::string& dottedName);   // 指针字段 s.ptr 的指向类型
    // 由 LLVM 结构体类型反查注册表名字
    std::string structNameOf(llvm::Type* structType);

    // 递归填充初始化列表 {1, 2, {3, 4}}（结构体按字段、数组按元素，支持嵌套）
    void fillInitList(llvm::Value* ptr, llvm::Type* targetTy, BlockStmtNode* block);

    // DWARF：DIBuilder 与当前子程序（generateStatement 设置 DebugLoc）
    llvm::DIBuilder* dib = nullptr;
    llvm::DICompileUnit* debugCU = nullptr;
    llvm::DISubprogram* currentSubprogram = nullptr;

    // goto/label：函数内 label → 基本块（generateFunction 预建）
    std::unordered_map<std::string, llvm::BasicBlock*> labelBlocks;
    void collectLabelBlocks(ASTNode* node, llvm::Function* func);
    void generateGoto(GotoStmtNode* node);
    void generateLabel(LabelStmtNode* node);
    void generateSwitch(SwitchStmtNode* node);

    // std.thread 内置调用生成
    llvm::Value* generateThreadBuiltin(FunctionCallNode* call);
    // std.atomic 原子内置调用生成
    llvm::Value* generateAtomicBuiltin(FunctionCallNode* call);
    llvm::AtomicOrdering atomicOrderOf(llvm::Value* orderVal);
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

    void generate(ProgramNode* root, bool emitMain = true);  // emitMain=false 用于库包（无入口）
    void setupDebugInfo();          // 模块级 DWARF 调试信息
    void setFunctionDebugInfo(llvm::Function* fn);  // 为函数建 DISubprogram
    void optimize(int optLevel);    // LLVM 优化 pass（0=不优化）
    void printIR();
    void saveToFile(const std::string& filename);
    bool verify();
    bool emitObject(const std::string& filename);
};

#endif
