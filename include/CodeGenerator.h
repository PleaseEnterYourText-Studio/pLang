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
    std::unordered_map<std::string, llvm::Value*> namedValues;

    llvm::Type* getLLVMType(ASTNodeType type);
    llvm::Type* getLLVMType(TypeNode* type);
    llvm::Type* getLLVMType(const std::string& typeName);

    llvm::Value* getVariable(const std::string& name);
    llvm::Value* generateExpression(ASTNode* node);
    void generateStatement(ASTNode* node);

public:
    CodeGenerator();
    ~CodeGenerator() = default;

    void generate(ProgramNode* root);
    void printIR();
    void saveToFile(const std::string& filename);
    bool verify();
};

#endif