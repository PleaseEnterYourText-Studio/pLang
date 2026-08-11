#include "../include/CodeGenerator.h"
#include <iostream>
#include "llvm/TargetParser/Host.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/TargetParser/Triple.h"

CodeGenerator::CodeGenerator()
    : builder(context), currentFunction(nullptr)
{
    module = std::make_unique<llvm::Module>("PLang", context);
}

llvm::Type* CodeGenerator::getLLVMType(ASTNodeType type)
{
    switch (type) {
        case ASTNodeType::TYPE_I8:
        case ASTNodeType::TYPE_U8:
        case ASTNodeType::TYPE_CHAR:
            return llvm::Type::getInt8Ty(context);
        case ASTNodeType::TYPE_I16:
        case ASTNodeType::TYPE_U16:
            return llvm::Type::getInt16Ty(context);
        case ASTNodeType::TYPE_I32:
        case ASTNodeType::TYPE_U32:
            return llvm::Type::getInt32Ty(context);
        case ASTNodeType::TYPE_BOOL:
            return llvm::Type::getInt1Ty(context);
        case ASTNodeType::TYPE_I64:
        case ASTNodeType::TYPE_U64:
            return llvm::Type::getInt64Ty(context);
        case ASTNodeType::TYPE_F32:
            return llvm::Type::getFloatTy(context);
        case ASTNodeType::TYPE_F64:
            return llvm::Type::getDoubleTy(context);
        case ASTNodeType::TYPE_VOID:
            return llvm::Type::getVoidTy(context);
        case ASTNodeType::TYPE_POINTER:
        case ASTNodeType::TYPE_STRING:
            return llvm::PointerType::get(context, 0);
        case ASTNodeType::TYPE_ARRAY:
            return llvm::ArrayType::get(llvm::Type::getInt32Ty(context), 0);
        default:
            return llvm::Type::getInt32Ty(context);
    }
}

llvm::Type* CodeGenerator::getLLVMType(TypeNode* type)
{
    if (!type) return llvm::Type::getInt32Ty(context);

    if (type->baseType == ASTNodeType::TYPE_POINTER) {
        return llvm::PointerType::get(context, 0);
    }

    if (type->baseType == ASTNodeType::TYPE_ARRAY) {
        auto innerType = getLLVMType(type->inner.get());
        int size = type->arraySize > 0 ? type->arraySize : 0;
        return llvm::ArrayType::get(innerType, size);
    }

    return getLLVMType(type->baseType);
}

llvm::Type* CodeGenerator::getLLVMType(const std::string& typeName)
{
    if (typeName == "i8" || typeName == "u8" || typeName == "char") {
        return llvm::Type::getInt8Ty(context);
    } else if (typeName == "i16" || typeName == "u16") {
        return llvm::Type::getInt16Ty(context);
    } else if (typeName == "i32" || typeName == "u32" || typeName == "int" || typeName == "uint") {
        return llvm::Type::getInt32Ty(context);
    } else if (typeName == "bool") {
        return llvm::Type::getInt1Ty(context);
    } else if (typeName == "i64" || typeName == "u64") {
        return llvm::Type::getInt64Ty(context);
    } else if (typeName == "f32") {
        return llvm::Type::getFloatTy(context);
    } else if (typeName == "f64") {
        return llvm::Type::getDoubleTy(context);
    } else if (typeName == "string" || typeName == "pointer") {
        return llvm::PointerType::get(context, 0);
    } else if (typeName == "void") {
        return llvm::Type::getVoidTy(context);
    }
    return llvm::Type::getInt32Ty(context);
}

llvm::Value* CodeGenerator::getVariable(const std::string& name)
{
    auto it = namedValues.find(name);
    if (it == namedValues.end()) {
        std::cerr << "Error: Variable '" << name << "' not declared" << std::endl;
        return nullptr;
    }
    return builder.CreateLoad(it->second.type, it->second.ptr, name);
}

llvm::Value* CodeGenerator::generateExpression(ASTNode* node)
{
    if (!node) return nullptr;

    switch (node->type) {
        case ASTNodeType::LITERAL_INT: {
            auto* lit = static_cast<LiteralIntNode*>(node);
            // 根据后缀决定宽度
            int bits = (lit->suffix == "ll" || lit->suffix == "LL") ? 64 : 32;
            return llvm::ConstantInt::get(context, llvm::APInt(bits, (uint64_t)lit->value, true));
        }

        case ASTNodeType::LITERAL_FLOAT: {
            auto* lit = static_cast<LiteralFloatNode*>(node);
            if (lit->suffix == "f") {
                return llvm::ConstantFP::get(context, llvm::APFloat((float)lit->value));
            }
            return llvm::ConstantFP::get(context, llvm::APFloat(lit->value));
        }

        case ASTNodeType::LITERAL_BOOL: {
            auto* lit = static_cast<LiteralBoolNode*>(node);
            return llvm::ConstantInt::get(context, llvm::APInt(1, lit->value ? 1 : 0));
        }

        case ASTNodeType::LITERAL_STRING: {
            auto* lit = static_cast<LiteralStringNode*>(node);
            if (lit->isChar) {
                // char 字面量: 取第一个字符的 ASCII 码
                char c = lit->value.empty() ? '\0' : lit->value[0];
                return llvm::ConstantInt::get(context, llvm::APInt(8, (uint64_t)(unsigned char)c));
            }
            return builder.CreateGlobalString(lit->value, "str");
        }

        case ASTNodeType::VARIABLE_REF: {
            auto* ref = static_cast<VariableRefNode*>(node);
            return getVariable(ref->name);
        }

        case ASTNodeType::BINARY_OP: {
            auto* bin = static_cast<BinaryOpNode*>(node);
            auto* left = generateExpression(bin->lift.get());
            auto* right = generateExpression(bin->right.get());

            switch (bin->op) {
                case BinaryOpType::ADD: return builder.CreateAdd(left, right, "add");
                case BinaryOpType::SUB: return builder.CreateSub(left, right, "sub");
                case BinaryOpType::MUL: return builder.CreateMul(left, right, "mul");
                case BinaryOpType::DIV: return builder.CreateSDiv(left, right, "div");
                case BinaryOpType::MOD: return builder.CreateSRem(left, right, "mod");
                case BinaryOpType::BITAND: return builder.CreateAnd(left, right, "and");
                case BinaryOpType::BITOR: return builder.CreateOr(left, right, "or");
                case BinaryOpType::BITXOR: return builder.CreateXor(left, right, "xor");
                case BinaryOpType::SHL: return builder.CreateShl(left, right, "shl");
                case BinaryOpType::SHR: return builder.CreateAShr(left, right, "shr");
            }
            break;
        }

        case ASTNodeType::UNARY_OP: {
            // 取地址 &a —— AddressOfNode 复用 UNARY_OP 类型
            if (auto* addr = dynamic_cast<AddressOfNode*>(node)) {
                if (addr->operand->type == ASTNodeType::VARIABLE_REF) {
                    auto* ref = static_cast<VariableRefNode*>(addr->operand.get());
                    auto it = namedValues.find(ref->name);
                    if (it != namedValues.end()) {
                        return it->second.ptr;
                    }
                }
                std::cerr << "Error: address-of requires a variable" << std::endl;
                return nullptr;
            }

            auto* unary = static_cast<UnaryOpNode*>(node);
            auto* operand = generateExpression(unary->operand.get());

            switch (unary->op) {
                case UnaryOpType::NEG:
                    return builder.CreateNeg(operand, "neg");
                case UnaryOpType::NOT:
                    return builder.CreateNot(operand, "not");
                case UnaryOpType::INC:
                case UnaryOpType::DEC: {
                    // 自增/自减: 需要作用在变量上
                    if (unary->operand->type == ASTNodeType::VARIABLE_REF) {
                        auto* ref = static_cast<VariableRefNode*>(unary->operand.get());
                        auto it = namedValues.find(ref->name);
                        if (it != namedValues.end()) {
                            llvm::Value* cur = builder.CreateLoad(it->second.type, it->second.ptr, ref->name);
                            llvm::Value* one = llvm::ConstantInt::get(it->second.type, 1, true);
                            llvm::Value* updated = (unary->op == UnaryOpType::INC)
                                ? builder.CreateAdd(cur, one, "inc")
                                : builder.CreateSub(cur, one, "dec");
                            builder.CreateStore(updated, it->second.ptr);
                            return updated;
                        }
                    }
                    break;
                }
                case UnaryOpType::DEREF: {
                    // 解引用 *p
                    if (operand) {
                        return builder.CreateLoad(llvm::Type::getInt32Ty(context), operand, "deref");
                    }
                    break;
                }
            }
            break;
        }

        case ASTNodeType::COMPARISON_OP: {
            auto* comp = static_cast<ComparisonOpNode*>(node);
            auto* left = generateExpression(comp->lift.get());
            auto* right = generateExpression(comp->right.get());

            llvm::CmpInst::Predicate pred;
            switch (comp->op) {
                case ComparisonOpType::EQ: pred = llvm::CmpInst::ICMP_EQ; break;
                case ComparisonOpType::NE: pred = llvm::CmpInst::ICMP_NE; break;
                case ComparisonOpType::LT: pred = llvm::CmpInst::ICMP_SLT; break;
                case ComparisonOpType::LE: pred = llvm::CmpInst::ICMP_SLE; break;
                case ComparisonOpType::GT: pred = llvm::CmpInst::ICMP_SGT; break;
                case ComparisonOpType::GE: pred = llvm::CmpInst::ICMP_SGE; break;
            }
            return builder.CreateICmp(pred, left, right, "cmp");
        }

        case ASTNodeType::LOGICAL_OP: {
            auto* logic = static_cast<LogicalOpNode*>(node);
            auto* left = generateExpression(logic->lift.get());
            auto* right = generateExpression(logic->right.get());

            switch (logic->op) {
                case LogicalOpType::AND: return builder.CreateAnd(left, right, "and");
                case LogicalOpType::OR: return builder.CreateOr(left, right, "or");
            }
            break;
        }

        case ASTNodeType::CAST: {
            auto* cast = static_cast<CastNode*>(node);
            llvm::Value* val = generateExpression(cast->value.get());
            if (!val) return nullptr;
            // 简单类型转换：按目标类型名处理
            llvm::Type* targetType = getLLVMType(cast->targetType);
            if (targetType->isIntegerTy() && val->getType()->isIntegerTy()) {
                return builder.CreateIntCast(val, targetType, true, "cast");
            }
            if (targetType->isDoubleTy()) {
                if (val->getType()->isIntegerTy())
                    return builder.CreateSIToFP(val, targetType, "cast");
            }
            if (val->getType()->isDoubleTy() && targetType->isIntegerTy()) {
                return builder.CreateFPToSI(val, targetType, "cast");
            }
            return val;
        }

        case ASTNodeType::BLOCK_STMT: {
            // 数组/结构体初始化 {1,2,3} —— 简化：取最后一个元素
            auto* block = static_cast<BlockStmtNode*>(node);
            llvm::Value* last = nullptr;
            for (auto& stmt : block->statements) {
                last = generateExpression(stmt.get());
            }
            if (last) return last;
            return llvm::ConstantInt::get(context, llvm::APInt(32, 0));
        }

        case ASTNodeType::FUNCTION_CALL: {
            auto* call = static_cast<FunctionCallNode*>(node);
            // 成员方法调用 obj.method() —— 结构体方法暂不完整实现
            if (call->name.find('.') != std::string::npos) {
                // 生成参数表达式但不调用
                for (auto& arg : call->arguments) {
                    generateExpression(arg.get());
                }
                return llvm::ConstantFP::get(context, llvm::APFloat(0.0));
            }

            llvm::Function* func = module->getFunction(call->name);
            if (!func) {
                std::cerr << "Error: Function '" << call->name << "' not found" << std::endl;
                return nullptr;
            }

            std::vector<llvm::Value*> args;
            for (auto& arg : call->arguments) {
                args.push_back(generateExpression(arg.get()));
            }

            return builder.CreateCall(func, args, "call");
        }

        default:
            std::cerr << "Warning: Unhandled expression type " << (int)node->type << std::endl;
            return nullptr;
    }

    return nullptr;
}

void CodeGenerator::generateStatement(ASTNode* node)
{
    if (!node) return;

    switch (node->type) {
        case ASTNodeType::EXPRESSION_STMT: {
            auto* stmt = static_cast<ExpressionStmtNode*>(node);
            generateExpression(stmt->expr.get());
            break;
        }

        case ASTNodeType::VARIABLE_DECL: {
            auto* decl = static_cast<VariableDeclNode*>(node);
            llvm::Type* varType = getLLVMType(ASTNodeType::TYPE_I32);
            if (decl->type) {
                varType = getLLVMType(decl->type.get());
            }

            llvm::AllocaInst* alloca = builder.CreateAlloca(varType, nullptr, decl->name);
            namedValues[decl->name] = VarInfo{alloca, varType};

            if (decl->initializer) {
                llvm::Value* initVal = generateExpression(decl->initializer.get());
                if (initVal) {
                    builder.CreateStore(initVal, alloca);
                }
            }
            break;
        }

        case ASTNodeType::ASSIGNMENT_STMT: {
            auto* assign = static_cast<AssignmentNode*>(node);
            llvm::Value* varPtr = nullptr;
            llvm::Type* varType = llvm::Type::getInt32Ty(context);
            if (assign->target->type == ASTNodeType::VARIABLE_REF) {
                auto* ref = static_cast<VariableRefNode*>(assign->target.get());
                auto it = namedValues.find(ref->name);
                if (it != namedValues.end()) {
                    varPtr = it->second.ptr;
                    varType = it->second.type;
                }
            } else if (assign->target->type == ASTNodeType::UNARY_OP) {
                // 解引用赋值 *p = v
                auto* unary = static_cast<UnaryOpNode*>(assign->target.get());
                if (unary->op == UnaryOpType::DEREF) {
                    varPtr = generateExpression(unary->operand.get());
                    varType = llvm::Type::getInt32Ty(context);
                }
            }

            if (varPtr) {
                llvm::Value* val = generateExpression(assign->value.get());
                if (val) {
                    builder.CreateStore(val, varPtr);
                }
            }
            break;
        }

        case ASTNodeType::RETURN_STMT: {
            auto* ret = static_cast<ReturnStmtNode*>(node);
            if (ret->value) {
                llvm::Value* val = generateExpression(ret->value.get());
                if (val) {
                    builder.CreateRet(val);
                }
            } else {
                builder.CreateRetVoid();
            }
            break;
        }

        case ASTNodeType::BLOCK_STMT: {
            auto* block = static_cast<BlockStmtNode*>(node);
            for (auto& stmt : block->statements) {
                generateStatement(stmt.get());
            }
            break;
        }

        case ASTNodeType::IF_STMT: {
            auto* ifStmt = static_cast<IfStmtNode*>(node);
            llvm::Value* cond = generateExpression(ifStmt->condition.get());
            if (!cond) return;

            llvm::Function* func = builder.GetInsertBlock()->getParent();

            llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(context, "then", func);
            llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(context, "else", func);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "merge", func);

            builder.CreateCondBr(cond, thenBB, elseBB);

            builder.SetInsertPoint(thenBB);
            generateStatement(ifStmt->thenBranch.get());
            if (!builder.GetInsertBlock()->getTerminator()) {
                builder.CreateBr(mergeBB);
            }

            builder.SetInsertPoint(elseBB);
            if (ifStmt->elseBranch) {
                generateStatement(ifStmt->elseBranch.get());
            }
            if (!builder.GetInsertBlock()->getTerminator()) {
                builder.CreateBr(mergeBB);
            }

            builder.SetInsertPoint(mergeBB);
            break;
        }

        case ASTNodeType::WHILE_STMT: {
            auto* whileStmt = static_cast<WhileStmtNode*>(node);
            llvm::Function* func = builder.GetInsertBlock()->getParent();

            llvm::BasicBlock* condBB = llvm::BasicBlock::Create(context, "while.cond", func);
            llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "while.body", func);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "while.merge", func);

            builder.CreateBr(condBB);

            builder.SetInsertPoint(condBB);
            llvm::Value* cond = generateExpression(whileStmt->condition.get());
            builder.CreateCondBr(cond, bodyBB, mergeBB);

            builder.SetInsertPoint(bodyBB);
            generateStatement(whileStmt->body.get());
            if (!builder.GetInsertBlock()->getTerminator()) {
                builder.CreateBr(condBB);
            }

            builder.SetInsertPoint(mergeBB);
            break;
        }

        case ASTNodeType::FOR_STMT: {
            auto* forStmt = static_cast<ForStmtNode*>(node);
            llvm::Function* func = builder.GetInsertBlock()->getParent();

            if (forStmt->init) {
                generateStatement(forStmt->init.get());
            }

            llvm::BasicBlock* condBB = llvm::BasicBlock::Create(context, "for.cond", func);
            llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "for.body", func);
            llvm::BasicBlock* updateBB = llvm::BasicBlock::Create(context, "for.update", func);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "for.merge", func);

            builder.CreateBr(condBB);

            builder.SetInsertPoint(condBB);
            llvm::Value* cond = forStmt->condition ?
                generateExpression(forStmt->condition.get()) :
                llvm::ConstantInt::get(context, llvm::APInt(1, 1));
            builder.CreateCondBr(cond, bodyBB, mergeBB);

            builder.SetInsertPoint(bodyBB);
            generateStatement(forStmt->body.get());
            if (!builder.GetInsertBlock()->getTerminator()) {
                builder.CreateBr(updateBB);
            }

            builder.SetInsertPoint(updateBB);
            if (forStmt->update) {
                generateExpression(forStmt->update.get());
            }
            builder.CreateBr(condBB);

            builder.SetInsertPoint(mergeBB);
            break;
        }

        default:
            std::cerr << "Warning: Unhandled statement type " << (int)node->type << std::endl;
            break;
    }
}

void CodeGenerator::generateFunction(FunctionDeclNode* fn)
{
    if (!fn || !fn->body) return;

    llvm::Type* retType = llvm::Type::getInt32Ty(context);
    if (fn->returnType) {
        retType = getLLVMType(fn->returnType.get());
    }

    std::vector<llvm::Type*> paramTypes;
    for (auto& param : fn->params) {
        paramTypes.push_back(getLLVMType(param->type.get()));
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(retType, paramTypes, false);
    llvm::Function* func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, fn->name, module.get());
    currentFunction = func;

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", func);
    builder.SetInsertPoint(entryBB);

    // 参数绑定
    size_t idx = 0;
    for (auto& arg : func->args()) {
        if (idx < fn->params.size()) {
            auto* param = fn->params[idx].get();
            llvm::AllocaInst* alloca = builder.CreateAlloca(arg.getType(), nullptr, param->name);
            builder.CreateStore(&arg, alloca);
            namedValues[param->name] = VarInfo{alloca, arg.getType()};
        }
        ++idx;
    }

    generateStatement(fn->body.get());

    if (!builder.GetInsertBlock()->getTerminator()) {
        if (retType->isVoidTy()) {
            builder.CreateRetVoid();
        } else {
            builder.CreateRet(llvm::ConstantInt::get(retType, 0));
        }
    }
}

void CodeGenerator::generate(ProgramNode* root)
{
    if (!root) return;

    // 先收集所有函数定义（不含 main，main 单独处理保证入口）
    for (auto& decl : root->decls)
    {
        if (decl->type == ASTNodeType::FUNCTION_DECL)
        {
            auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get());
            if (fn->name != "main")
            {
                generateFunction(fn);
            }
        }
    }

    // main 函数作为入口，返回 int
    llvm::FunctionType* mainType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(context), false);
    llvm::Function* mainFunc = llvm::Function::Create(
        mainType, llvm::Function::ExternalLinkage, "main", module.get());
    currentFunction = mainFunc;

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", mainFunc);
    builder.SetInsertPoint(entryBB);

    for (auto& decl : root->decls)
    {
        if (decl->type == ASTNodeType::FUNCTION_DECL)
        {
            auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get());
            if (fn->name == "main" && fn->body)
            {
                generateStatement(fn->body.get());
            }
        }
    }

    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateRet(llvm::ConstantInt::get(context, llvm::APInt(32, 0)));
    }
}

void CodeGenerator::printIR()
{
    module->print(llvm::outs(), nullptr);
}

void CodeGenerator::saveToFile(const std::string& filename)
{
    std::error_code EC;
    llvm::raw_fd_ostream out(filename, EC);
    if (EC) {
        std::cerr << "Error opening file: " << EC.message() << std::endl;
        return;
    }
    module->print(out, nullptr);
}

bool CodeGenerator::verify()
{
    std::string error;
    llvm::raw_string_ostream errStream(error);
    if (llvm::verifyModule(*module, &errStream)) {
        std::cerr << "Verification failed:\n" << error << std::endl;
        return false;
    }
    return true;
}

bool CodeGenerator::emitObject(const std::string& filename)
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    llvm::Triple triple(targetTriple);
    module->setTargetTriple(triple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        std::cerr << "target error: " << error << std::endl;
        return false;
    }

    auto cpu = "generic";
    auto features = "";
    llvm::TargetOptions opt;
    auto relocModel = llvm::Reloc::PIC_;
    auto targetMachine = target->createTargetMachine(
        triple, cpu, features, opt, relocModel);

    module->setDataLayout(targetMachine->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::cerr << "cannot open " << filename << ": " << ec.message() << std::endl;
        return false;
    }

    llvm::legacy::PassManager pass;
    auto fileType = llvm::CodeGenFileType::ObjectFile;
    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        std::cerr << "target machine cannot emit file" << std::endl;
        return false;
    }
    pass.run(*module);
    dest.flush();
    return true;
}
