#include "../include/CodeGenerator.h"
#include <iostream>
#include <algorithm>
#include "llvm/TargetParser/Host.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/IR/InlineAsm.h"

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

    if (type->baseType == ASTNodeType::TYPE_PRIMITIVE) {
        // 用户命名类型/ptr：按名字映射
        return getLLVMType(type->name);
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
    } else if (typeName == "string" || typeName == "pointer" || typeName == "ptr") {
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

        case ASTNodeType::LITERAL_NULL: {
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0));
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
            if (!left || !right) break;

            llvm::Type* commonTy = unifyOperands(left, right);
            bool isFloat = commonTy && commonTy->isFloatingPointTy();

            switch (bin->op) {
                case BinaryOpType::ADD: return isFloat ? builder.CreateFAdd(left, right, "add") : builder.CreateAdd(left, right, "add");
                case BinaryOpType::SUB: return isFloat ? builder.CreateFSub(left, right, "sub") : builder.CreateSub(left, right, "sub");
                case BinaryOpType::MUL: return isFloat ? builder.CreateFMul(left, right, "mul") : builder.CreateMul(left, right, "mul");
                case BinaryOpType::DIV: return isFloat ? builder.CreateFDiv(left, right, "div") : builder.CreateSDiv(left, right, "div");
                case BinaryOpType::MOD: return isFloat ? builder.CreateFRem(left, right, "mod") : builder.CreateSRem(left, right, "mod");
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
            if (!left || !right) break;

            llvm::Type* commonTy = unifyOperands(left, right);

            // 浮点比较
            if (commonTy && commonTy->isFloatingPointTy()) {
                llvm::FCmpInst::Predicate fpred;
                switch (comp->op) {
                    case ComparisonOpType::EQ: fpred = llvm::FCmpInst::FCMP_OEQ; break;
                    case ComparisonOpType::NE: fpred = llvm::FCmpInst::FCMP_ONE; break;
                    case ComparisonOpType::LT: fpred = llvm::FCmpInst::FCMP_OLT; break;
                    case ComparisonOpType::LE: fpred = llvm::FCmpInst::FCMP_OLE; break;
                    case ComparisonOpType::GT: fpred = llvm::FCmpInst::FCMP_OGT; break;
                    case ComparisonOpType::GE: fpred = llvm::FCmpInst::FCMP_OGE; break;
                }
                return builder.CreateFCmp(fpred, left, right, "cmp");
            }

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

            // std.thread 编译器内置调用（spawn / sleep / mutex.create）
            if (call->name == "thread.spawn" || call->name == "thread.sleep" ||
                call->name == "thread.mutex.create") {
                return generateThreadBuiltin(call);
            }

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

        case ASTNodeType::ASM_STMT: {
            auto* asmNode = static_cast<AsmNode*>(node);

            // 组装 LLVM inline asm
            std::string constraints;
            // 输出约束
            for (size_t i = 0; i < asmNode->outputs.size(); ++i) {
                if (i > 0) constraints += ",";
                constraints += asmNode->outputs[i].constraint;
            }
            // 输入约束
            for (size_t i = 0; i < asmNode->inputs.size(); ++i) {
                if (!constraints.empty()) constraints += ",";
                constraints += asmNode->inputs[i].constraint;
            }
            // clobber
            for (size_t i = 0; i < asmNode->clobbers.size(); ++i) {
                if (!constraints.empty()) constraints += ",";
                constraints += "~{" + asmNode->clobbers[i] + "}";
            }

            // 操作数值：只有输入操作数需要传参数
            std::vector<llvm::Value*> args;
            std::vector<llvm::Type*> argTypes;
            for (auto& in : asmNode->inputs) {
                auto it = namedValues.find(in.name);
                llvm::Type* t = llvm::Type::getInt32Ty(context);
                if (it != namedValues.end()) {
                    t = it->second.type;
                    args.push_back(builder.CreateLoad(it->second.type, it->second.ptr, in.name));
                } else {
                    args.push_back(llvm::ConstantInt::get(context, llvm::APInt(32, 0)));
                }
                argTypes.push_back(t);
            }

            // 返回类型：有输出时返回输出类型，无输出则 void
            llvm::Type* retType = llvm::Type::getVoidTy(context);
            if (!asmNode->outputs.empty()) {
                if (asmNode->outputs.size() == 1) {
                    auto it = namedValues.find(asmNode->outputs[0].name);
                    retType = (it != namedValues.end()) ? it->second.type : llvm::Type::getInt32Ty(context);
                } else {
                    std::vector<llvm::Type*> outTypes;
                    for (auto& out : asmNode->outputs) {
                        auto it = namedValues.find(out.name);
                        outTypes.push_back((it != namedValues.end()) ? it->second.type
                                                                     : llvm::Type::getInt32Ty(context));
                    }
                    retType = llvm::StructType::get(context, outTypes);
                }
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
            auto* asmCall = llvm::InlineAsm::get(ft, asmNode->template_str, constraints, true);
            auto* callResult = builder.CreateCall(asmCall, args, "asm");

            // 输出操作数写回变量（从返回值提取）
            size_t outIdx = 0;
            for (auto& out : asmNode->outputs) {
                auto it = namedValues.find(out.name);
                if (it != namedValues.end() && callResult) {
                    llvm::Value* val = callResult;
                    if (asmNode->outputs.size() > 1) {
                        val = builder.CreateExtractValue(callResult, outIdx, "asm_out");
                    }
                    builder.CreateStore(val, it->second.ptr);
                }
                ++outIdx;
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
                if (fn->body)
                {
                    generateFunction(fn);
                }
                else
                {
                    // 无函数体（extern FFI 或待 impl 的声明）：仅 emit 外部声明
                    llvm::Type* retType = llvm::Type::getInt32Ty(context);
                    if (fn->returnType) {
                        retType = getLLVMType(fn->returnType.get());
                    }
                    std::vector<llvm::Type*> paramTypes;
                    for (auto& param : fn->params) {
                        paramTypes.push_back(getLLVMType(param->type.get()));
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(retType, paramTypes, false);
                    if (!module->getFunction(fn->name)) {
                        llvm::Function::Create(ft, llvm::Function::ExternalLinkage, fn->name, module.get());
                    }
                }
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

// 统一两个操作数的类型：整数拓宽 / 整数↔浮点提升，返回公共类型（无法统一时返回 nullptr）
llvm::Type* CodeGenerator::unifyOperands(llvm::Value*& left, llvm::Value*& right)
{
    if (left->getType() == right->getType()) return left->getType();

    if (left->getType()->isIntegerTy() && right->getType()->isIntegerTy()) {
        unsigned leftBits = left->getType()->getIntegerBitWidth();
        unsigned rightBits = right->getType()->getIntegerBitWidth();
        unsigned targetBits = std::max(leftBits, rightBits);
        llvm::Type* targetTy = llvm::Type::getIntNTy(context, targetBits);
        if (leftBits < targetBits) left = builder.CreateIntCast(left, targetTy, true, "promoteLeft");
        if (rightBits < targetBits) right = builder.CreateIntCast(right, targetTy, true, "promoteRight");
        return targetTy;
    }

    if (left->getType()->isFloatingPointTy() && right->getType()->isIntegerTy()) {
        right = builder.CreateSIToFP(right, left->getType(), "toFloatRight");
        return left->getType();
    }
    if (right->getType()->isFloatingPointTy() && left->getType()->isIntegerTy()) {
        left = builder.CreateSIToFP(left, right->getType(), "toFloatLeft");
        return right->getType();
    }

    return nullptr;
}

// std.thread 编译器内置支持

// 获取（或声明）一个外部 C 函数
llvm::Function* CodeGenerator::getOrDeclareFunction(const std::string& name,
                                                    llvm::Type* retType,
                                                    const std::vector<llvm::Type*>& paramTypes)
{
    if (auto* existing = module->getFunction(name))
    {
        return existing;
    }
    llvm::FunctionType* ft = llvm::FunctionType::get(retType, paramTypes, false);
    return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
}

// 互斥锁池：[64 x [64 x i8]]，每个槽位可容纳一个 pthread_mutex_t（全局唯一）
llvm::GlobalVariable* CodeGenerator::getMutexPool()
{
    llvm::Type* slotTy = llvm::ArrayType::get(llvm::Type::getInt8Ty(context), 64);
    llvm::Type* poolTy = llvm::ArrayType::get(slotTy, 64);
    llvm::Constant* pool = module->getOrInsertGlobal("plangMutexPool", poolTy);
    if (auto* global = llvm::dyn_cast<llvm::GlobalVariable>(pool))
    {
        if (global->hasInitializer() == false)
        {
            global->setInitializer(llvm::ConstantAggregateZero::get(poolTy));
            global->setLinkage(llvm::GlobalValue::InternalLinkage);
        }
        return global;
    }
    return nullptr;
}

// 由槽位句柄（i64 下标）计算池内地址并转为 i8*
llvm::Value* CodeGenerator::getMutexSlotAddress(llvm::Value* slotValue)
{
    llvm::GlobalVariable* pool = getMutexPool();
    llvm::PointerType* ptrTy = llvm::PointerType::get(context, 0);
    if (!pool)
    {
        std::cerr << "Error: failed to create mutex pool" << std::endl;
        return llvm::ConstantPointerNull::get(ptrTy);
    }
    llvm::Value* slot = builder.CreateIntCast(slotValue, llvm::Type::getInt64Ty(context), true, "mutexSlot");
    std::vector<llvm::Value*> indices = {
        builder.getInt32(0),
        builder.CreateIntCast(slot, llvm::Type::getInt32Ty(context), true, "mutexSlotIdx")
    };
    llvm::Value* slotPtr = builder.CreateGEP(pool->getValueType(), pool, indices, "mutexSlotPtr");
    return builder.CreateBitCast(slotPtr, ptrTy, "mutexSlotI8");
}

// 生成 thread.* 编译器内置调用（仅 spawn / sleep / mutex.create）
llvm::Value* CodeGenerator::generateThreadBuiltin(FunctionCallNode* call)
{
    const std::string& name = call->name;
    llvm::PointerType* ptrTy = llvm::PointerType::get(context, 0);
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(ptrTy);

    // thread.spawn(fn)：启动线程运行无参函数 fn，返回线程句柄（pointer）
    if (name == "thread.spawn")
    {
        if (call->arguments.size() != 1)
        {
            std::cerr << "Error: thread.spawn expects 1 argument" << std::endl;
            return nullPtr;
        }
        auto* ref = dynamic_cast<VariableRefNode*>(call->arguments[0].get());
        if (!ref)
        {
            std::cerr << "Error: thread.spawn expects a function name" << std::endl;
            return nullPtr;
        }
        llvm::Function* target = module->getFunction(ref->name);
        if (!target)
        {
            std::cerr << "Error: thread.spawn target '" << ref->name << "' not found" << std::endl;
            return nullPtr;
        }

        // 蹦床函数：void* trampoline(void*) { target(); return nullptr; }
        std::string trampName = "plangThreadTrampoline" + std::to_string(threadTrampolineCounter++);
        llvm::FunctionType* trampTy = llvm::FunctionType::get(ptrTy, {ptrTy}, false);
        llvm::Function* tramp = llvm::Function::Create(
            trampTy, llvm::Function::InternalLinkage, trampName, module.get());

        llvm::BasicBlock* savedBlock = builder.GetInsertBlock();
        llvm::BasicBlock::iterator savedPoint = builder.GetInsertPoint();

        llvm::BasicBlock* trampEntry = llvm::BasicBlock::Create(context, "entry", tramp);
        builder.SetInsertPoint(trampEntry);
        builder.CreateCall(target, {});
        builder.CreateRet(nullPtr);

        builder.SetInsertPoint(savedBlock, savedPoint);

        // pthread_create(&handle, nullptr, trampoline, nullptr)
        llvm::Function* pthreadCreate = getOrDeclareFunction(
            "pthread_create", llvm::Type::getInt32Ty(context), {ptrTy, ptrTy, ptrTy, ptrTy});
        llvm::AllocaInst* handle = builder.CreateAlloca(ptrTy, nullptr, "threadHandle");
        builder.CreateCall(pthreadCreate, {handle, nullPtr, builder.CreateBitCast(tramp, ptrTy), nullPtr});
        return builder.CreateLoad(ptrTy, handle, "threadId");
    }

    // thread.sleep(ms)：休眠指定毫秒
    if (name == "thread.sleep")
    {
        llvm::Value* ms = generateExpression(call->arguments[0].get());
        ms = builder.CreateIntCast(ms, llvm::Type::getInt64Ty(context), true, "sleepMs");

        // struct timespec { time_t tv_sec; long tv_nsec; }
        llvm::Type* tsTy = llvm::ArrayType::get(llvm::Type::getInt64Ty(context), 2);
        llvm::AllocaInst* ts = builder.CreateAlloca(tsTy, nullptr, "timespec");
        llvm::Value* sec = builder.CreateSDiv(ms, llvm::ConstantInt::get(context, llvm::APInt(64, 1000)), "sleepSec");
        llvm::Value* rem = builder.CreateSRem(ms, llvm::ConstantInt::get(context, llvm::APInt(64, 1000)), "sleepRem");
        llvm::Value* nsec = builder.CreateMul(rem, llvm::ConstantInt::get(context, llvm::APInt(64, 1000000)), "sleepNsec");
        std::vector<llvm::Value*> secIdx = {builder.getInt32(0), builder.getInt32(0)};
        std::vector<llvm::Value*> nsecIdx = {builder.getInt32(0), builder.getInt32(1)};
        builder.CreateStore(sec, builder.CreateGEP(tsTy, ts, secIdx, "tvSecPtr"));
        builder.CreateStore(nsec, builder.CreateGEP(tsTy, ts, nsecIdx, "tvNsecPtr"));

        llvm::Function* nanosleep = getOrDeclareFunction(
            "nanosleep", llvm::Type::getInt32Ty(context), {ptrTy, ptrTy});
        builder.CreateCall(nanosleep, {builder.CreateBitCast(ts, ptrTy), nullPtr});
        return nullptr;
    }

    // thread.mutex.create()：从池中分配并初始化一把互斥锁，返回其地址（pointer）
    if (name == "thread.mutex.create")
    {
        if (mutexSlotCount >= 64)
        {
            std::cerr << "Error: mutex pool exhausted (max 64)" << std::endl;
            return nullPtr;
        }
        int slot = mutexSlotCount++;
        llvm::Value* slotAddr = getMutexSlotAddress(
            llvm::ConstantInt::get(context, llvm::APInt(64, (uint64_t)slot, true)));
        llvm::Function* mutexInit = getOrDeclareFunction(
            "pthread_mutex_init", llvm::Type::getInt32Ty(context), {ptrTy, ptrTy});
        builder.CreateCall(mutexInit, {slotAddr, nullPtr});
        return slotAddr;
    }

    std::cerr << "Error: unknown std.thread builtin '" << name << "'" << std::endl;
    return nullPtr;
}
