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

// 类型大小（字节）：用于 union 取最大字段
static unsigned typeSizeBytes(llvm::Type* ty);

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
    } else if (typeName == "string" || typeName == "pointer" || typeName == "ptr" || typeName == "func") {
        return llvm::PointerType::get(context, 0);
    } else if (typeName == "void") {
        return llvm::Type::getVoidTy(context);
    }
    // 结构体类型（由 generate() 预扫描登记）
    auto sIt = structDefs.find(typeName);
    if (sIt != structDefs.end()) return sIt->second.type;
    return llvm::Type::getInt32Ty(context);
}


llvm::Value* CodeGenerator::getVariable(const std::string& name)
{
    // 结构体成员访问 s.x / a.b.c：逐级解析 + load（位域按宽度掩码）
    size_t dot = name.find('.');
    if (dot != std::string::npos)
    {
        llvm::Type* fieldTy = nullptr;
        int bitWidth = 0;
        llvm::Value* fp = getMemberAddress(name, fieldTy, bitWidth);
        if (fp && fieldTy) {
            llvm::Value* v = builder.CreateLoad(fieldTy, fp, "member");
            if (bitWidth > 0)
            {
                llvm::Value* mask = llvm::ConstantInt::get(fieldTy,
                    llvm::APInt(fieldTy->getIntegerBitWidth(), ((uint64_t)1 << bitWidth) - 1, false));
                v = builder.CreateAnd(v, mask, "bitfield");
            }
            return v;
        }
        std::cerr << "Error: member access '" << name << "' failed" << std::endl;
        return nullptr;
    }

    auto it = namedValues.find(name);
    if (it != namedValues.end()) {
        bool isVol = volatileVars.count(name) > 0;
        llvm::LoadInst* li = builder.CreateLoad(it->second.type, it->second.ptr, name);
        if (isVol) li->setVolatile(true);
        return li;
    }
    // extern 全局数据（如 stdin）：加载全局存储的值
    auto gIt = externGlobals.find(name);
    if (gIt != externGlobals.end()) {
        return builder.CreateLoad(gIt->second->getValueType(), gIt->second, name);
    }
    std::cerr << "Error: Variable '" << name << "' not declared" << std::endl;
    return nullptr;
}


llvm::Value* CodeGenerator::generateExpression(ASTNode* node)
{
    if (!node) return nullptr;

    switch (node->type) {
        case ASTNodeType::LITERAL_INT: {
            auto* lit = static_cast<LiteralIntNode*>(node);
            // 根据后缀决定宽度；无后缀但放不下 32 位时自动拓宽为 64 位
            int bits = (lit->suffix == "ll" || lit->suffix == "LL") ? 64 : 32;
            if (bits == 32 && (lit->value > 2147483647LL || lit->value < -2147483648LL))
            {
                bits = 64;
            }
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

        case ASTNodeType::INDEX: {
            auto* idxNode = static_cast<IndexNode*>(node);
            llvm::Type* elemType = nullptr;
            llvm::Value* elemPtr = getIndexedAddress(idxNode, elemType);
            if (elemPtr && elemType) {
                return builder.CreateLoad(elemType, elemPtr, "elem");
            }
            std::cerr << "Error: invalid array index expression" << std::endl;
            return nullptr;
        }

        case ASTNodeType::BINARY_OP: {
            auto* bin = static_cast<BinaryOpNode*>(node);
            auto* left = generateExpression(bin->lift.get());
            auto* right = generateExpression(bin->right.get());
            if (!left || !right) break;

            llvm::Type* commonTy = unifyOperands(left, right);
            bool isFloat = commonTy && commonTy->isFloatingPointTy();

            // 指针算术：p + n / n + p / p - n（按元素大小 GEP 缩放）
            if (bin->op == BinaryOpType::ADD || bin->op == BinaryOpType::SUB)
            {
                if (left->getType()->isPointerTy() && right->getType()->isIntegerTy())
                {
                    llvm::Type* elemTy = pointerElementType(bin->lift.get());
                    llvm::Value* idx = right;
                    if (bin->op == BinaryOpType::SUB) idx = builder.CreateNeg(idx, "negIdx");
                    return builder.CreateGEP(elemTy, left, idx, "ptrAdd");
                }
                if (bin->op == BinaryOpType::ADD &&
                    right->getType()->isPointerTy() && left->getType()->isIntegerTy())
                {
                    llvm::Type* elemTy = pointerElementType(bin->right.get());
                    return builder.CreateGEP(elemTy, right, left, "ptrAdd");
                }
            }

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
                    // &函数名 —— 函数指针
                    if (auto* fn = module->getFunction(ref->name)) {
                        return fn;
                    }
                    // &s.x / &a.b.c —— 结构体成员地址
                    size_t dot = ref->name.find('.');
                    if (dot != std::string::npos)
                    {
                        llvm::Type* fieldTy = nullptr;
                        int bw = 0;
                        llvm::Value* fp = getMemberAddress(ref->name, fieldTy, bw);
                        if (fp) return fp;
                        std::cerr << "Error: address-of member '" << ref->name << "' not found" << std::endl;
                        return nullptr;
                    }
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
                case UnaryOpType::NEG: {
                    // 常量取负：直接折叠成负常量，避免常量表达式 sub (0.0, x) 无法选择
                    if (auto* cf = llvm::dyn_cast<llvm::ConstantFP>(operand)) {
                        return llvm::ConstantFP::get(context, -cf->getValueAPF());
                    }
                    if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(operand)) {
                        return llvm::ConstantInt::get(context, -ci->getValue());
                    }
                    return builder.CreateNeg(operand, "neg");
                }
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
                    // 解引用 *p：按指向类型读取
                    if (operand) {
                        llvm::Type* loadTy = pointerElementType(unary->operand.get());
                        return builder.CreateLoad(loadTy, operand, "deref");
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

            // std.atomic 原子内置调用
            if (call->name.rfind("atomic.", 0) == 0) {
                return generateAtomicBuiltin(call);
            }

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
                // 间接调用：通过函数指针变量 cb(...)
                auto it = namedValues.find(call->name);
                if (it != namedValues.end() && it->second.type->isPointerTy())
                {
                    llvm::Value* fnPtr = builder.CreateLoad(it->second.type, it->second.ptr, call->name);
                    std::vector<llvm::Type*> argTys;
                    std::vector<llvm::Value*> args;
                    for (auto& arg : call->arguments) {
                        llvm::Value* v = generateExpression(arg.get());
                        args.push_back(v);
                        if (v) argTys.push_back(v->getType());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(context), argTys, false);
                    return builder.CreateCall(ft, fnPtr, args); // void 返回不带名字
                }
                std::cerr << "Error: Function '" << call->name << "' not found" << std::endl;
                return nullptr;
            }

            std::vector<llvm::Value*> args;
            for (size_t i = 0; i < call->arguments.size(); ++i) {
                llvm::Value* v = generateExpression(call->arguments[i].get());
                if (v && i < func->getFunctionType()->getNumParams()) {
                    llvm::Type* paramTy = func->getFunctionType()->getParamType(i);
                    v = coerceValue(v, paramTy);
                }
                args.push_back(v);
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
                // 记录指针变量指向类型（buf[i] 用）
                if (decl->type->baseType == ASTNodeType::TYPE_POINTER && decl->type->inner) {
                    namedValueElementTypes[decl->name] = getLLVMType(decl->type->inner.get());
                }
            }

            // 结构体对齐 align(N)：作用于该类型变量的 alloca
            unsigned align = 0;
            if (varType->isStructTy())
            {
                std::string sname = structNameOf(varType);
                if (!sname.empty()) align = (unsigned)structDefs[sname].alignBytes;
            }
            llvm::AllocaInst* alloca = builder.CreateAlloca(varType, nullptr, decl->name);
            if (align > 0) alloca->setAlignment(llvm::Align(align));
            namedValues[decl->name] = VarInfo{alloca, varType};
            if (decl->isVolatile) volatileVars.insert(decl->name);

            if (decl->initializer) {
                if (decl->initializer->type == ASTNodeType::BLOCK_STMT &&
                    (varType->isStructTy() || varType->isArrayTy()))
                {
                    // 结构体/数组初始化列表：递归逐字段/元素填充（支持嵌套）
                    auto* block = static_cast<BlockStmtNode*>(decl->initializer.get());
                    fillInitList(alloca, varType, block);
                }
                else
                {
                    llvm::Value* initVal = generateExpression(decl->initializer.get());
                    if (initVal) {
                        builder.CreateStore(initVal, alloca, decl->isVolatile);
                    }
                }
            }
            break;
        }

        case ASTNodeType::ASSIGNMENT_STMT: {
            auto* assign = static_cast<AssignmentNode*>(node);
            llvm::Value* varPtr = nullptr;
            llvm::Type* varType = llvm::Type::getInt32Ty(context);
            int memberBitWidth = 0;
            bool assignVolatile = false;
            if (assign->target->type == ASTNodeType::VARIABLE_REF) {
                auto* ref = static_cast<VariableRefNode*>(assign->target.get());
                if (ref->name.find('.') != std::string::npos) {
                    // 成员赋值 s.x = v（位域按宽度掩码写）
                    llvm::Type* fieldTy = nullptr;
                    int bitWidth = 0;
                    varPtr = getMemberAddress(ref->name, fieldTy, bitWidth);
                    varType = fieldTy ? fieldTy : llvm::Type::getInt32Ty(context);
                    memberBitWidth = bitWidth;
                } else {
                    auto it = namedValues.find(ref->name);
                    if (it != namedValues.end()) {
                        varPtr = it->second.ptr;
                        varType = it->second.type;
                    }
                }
                assignVolatile = volatileVars.count(ref->name) > 0;
            } else if (assign->target->type == ASTNodeType::UNARY_OP) {
                // 解引用赋值 *p = v：按指向类型存储
                auto* unary = static_cast<UnaryOpNode*>(assign->target.get());
                if (unary->op == UnaryOpType::DEREF) {
                    varPtr = generateExpression(unary->operand.get());
                    varType = pointerElementType(unary->operand.get());
                }
            } else if (assign->target->type == ASTNodeType::INDEX) {
                // 数组下标赋值 buf[i] = v
                auto* idxNode = static_cast<IndexNode*>(assign->target.get());
                varPtr = getIndexedAddress(idxNode, varType);
            }

            if (varPtr) {
                llvm::Value* val = generateExpression(assign->value.get());
                if (val) {
                    // 值类型与元素类型不一致（如 char 元素存 int）时按元素类型转换
                    if (val->getType() != varType && varType->isIntegerTy() && val->getType()->isIntegerTy()) {
                        val = builder.CreateIntCast(val, varType, true, "storeCast");
                    }
                    // 位域：保留同单元其他位，写入低 N 位
                    if (memberBitWidth > 0 && varType->isIntegerTy())
                    {
                        llvm::Value* cell = builder.CreateLoad(varType, varPtr, "bitfieldCell");
                        llvm::APInt ap((unsigned)varType->getIntegerBitWidth(), (uint64_t)1 << memberBitWidth, false);
                        llvm::Value* mask = llvm::ConstantInt::get(varType, ap - 1);
                        llvm::Value* cleared = builder.CreateAnd(cell, builder.CreateNot(mask), "bitfieldClear");
                        val = builder.CreateOr(cleared, builder.CreateAnd(val, mask), "bitfieldSet");
                    }
                    builder.CreateStore(val, varPtr, assignVolatile);
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
                // 前一条语句已终结（return/goto）且当前不是 label：进入不可达块
                if (builder.GetInsertBlock()->getTerminator() &&
                    stmt->type != ASTNodeType::LABEL_STMT)
                {
                    llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(
                        context, "dead", builder.GetInsertBlock()->getParent());
                    builder.SetInsertPoint(deadBB);
                }
                generateStatement(stmt.get());
            }
            break;
        }
        case ASTNodeType::GOTO_STMT: { generateGoto(static_cast<GotoStmtNode*>(node)); break; }
        case ASTNodeType::LABEL_STMT: { generateLabel(static_cast<LabelStmtNode*>(node)); break; }
        case ASTNodeType::SWITCH_STMT: { generateSwitch(static_cast<SwitchStmtNode*>(node)); break; }

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

// 预扫描函数体收集 label 并创建对应基本块
void CodeGenerator::collectLabelBlocks(ASTNode* node, llvm::Function* func)
{
    if (!node) return;
    if (node->type == ASTNodeType::LABEL_STMT)
    {
        auto* label = dynamic_cast<LabelStmtNode*>(node);
        if (!labelBlocks.count(label->name))
        {
            labelBlocks[label->name] = llvm::BasicBlock::Create(context, "label." + label->name, func);
        }
        return;
    }
    if (node->type == ASTNodeType::BLOCK_STMT)
    {
        for (auto& st : dynamic_cast<BlockStmtNode*>(node)->statements) collectLabelBlocks(st.get(), func);
    }
    else if (node->type == ASTNodeType::IF_STMT)
    {
        auto* ifn = dynamic_cast<IfStmtNode*>(node);
        collectLabelBlocks(ifn->thenBranch.get(), func);
        collectLabelBlocks(ifn->elseBranch.get(), func);
    }
    else if (node->type == ASTNodeType::WHILE_STMT)
    {
        collectLabelBlocks(dynamic_cast<WhileStmtNode*>(node)->body.get(), func);
    }
    else if (node->type == ASTNodeType::FOR_STMT)
    {
        auto* fn = dynamic_cast<ForStmtNode*>(node);
        collectLabelBlocks(fn->init.get(), func);
        collectLabelBlocks(fn->body.get(), func);
    }
    else if (node->type == ASTNodeType::SWITCH_STMT)
    {
        for (auto& c : dynamic_cast<SwitchStmtNode*>(node)->cases) collectLabelBlocks(c.body.get(), func);
    }
}

void CodeGenerator::generateGoto(GotoStmtNode* node)
{
    auto it = labelBlocks.find(node->label);
    if (it != labelBlocks.end())
    {
        builder.CreateBr(it->second);
    }
}

void CodeGenerator::generateLabel(LabelStmtNode* node)
{
    auto it = labelBlocks.find(node->name);
    if (it == labelBlocks.end()) return;
    // 落入 label：当前块（未终结时）跳到 label 块，再从 label 块继续
    if (!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(it->second);
    }
    builder.SetInsertPoint(it->second);
}

void CodeGenerator::generateSwitch(SwitchStmtNode* node)
{
    llvm::Function* func = builder.GetInsertBlock()->getParent();
    llvm::Value* cond = generateExpression(node->condition.get());
    if (!cond) return;

    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "switch.merge", func);
    llvm::SwitchInst* si = builder.CreateSwitch(cond, mergeBB, (unsigned)node->cases.size());

    for (auto& c : node->cases)
    {
        if (c.isDefault)
        {
            // default 分支体直接在当前 default 块（merge 兼任无 default 的落点）
            if (c.body)
            {
                llvm::BasicBlock* defBB = llvm::BasicBlock::Create(context, "switch.default", func);
                si->setDefaultDest(defBB);
                builder.SetInsertPoint(defBB);
                generateStatement(c.body.get());
                if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(mergeBB);
            }
        }
        else
        {
            llvm::BasicBlock* caseBB = llvm::BasicBlock::Create(context, "switch.case", func);
            auto* caseVal = llvm::cast<llvm::ConstantInt>(
                llvm::ConstantInt::get(cond->getType(), (uint64_t)c.value, true));
            si->addCase(caseVal, caseBB);
            builder.SetInsertPoint(caseBB);
            if (c.body) generateStatement(c.body.get());
            if (!builder.GetInsertBlock()->getTerminator()) builder.CreateBr(mergeBB);
        }
    }
    builder.SetInsertPoint(mergeBB);
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

    llvm::Function* func = module->getFunction(fn->name);
    if (!func) {
        llvm::FunctionType* funcType = llvm::FunctionType::get(retType, paramTypes, fn->isVariadic);
        func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, fn->name, module.get());
    }
    currentFunction = func;
    labelBlocks.clear();
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", func);
    builder.SetInsertPoint(entryBB);
    collectLabelBlocks(fn->body.get(), func);

    // 参数绑定
    size_t idx = 0;
    for (auto& arg : func->args()) {
        if (idx < fn->params.size()) {
            auto* param = fn->params[idx].get();
            llvm::AllocaInst* alloca = builder.CreateAlloca(arg.getType(), nullptr, param->name);
            builder.CreateStore(&arg, alloca);
            namedValues[param->name] = VarInfo{alloca, arg.getType()};
            // 记录指针参数指向类型（buf[i] 用）
            if (param->type && param->type->baseType == ASTNodeType::TYPE_POINTER && param->type->inner) {
                namedValueElementTypes[param->name] = getLLVMType(param->type->inner.get());
            }
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

    // 预扫描：先建立结构体类型（两遍，支持结构体嵌套/前向引用）
    std::vector<StructDeclNode*> structDecls;
    for (auto& decl : root->decls)
    {
        StructDeclNode* sn = nullptr;
        if (decl->type == ASTNodeType::STRUCT_DECL) sn = dynamic_cast<StructDeclNode*>(decl.get());
        else if (decl->type == ASTNodeType::USING_DECL)
        {
            auto* u = dynamic_cast<UsingDeclNode*>(decl.get());
            if (u->aliased && u->aliased->type == ASTNodeType::STRUCT_DECL)
                sn = dynamic_cast<StructDeclNode*>(u->aliased.get());
        }
        if (sn && !sn->name.empty() && !structDefs.count(sn->name))
        {
            structDecls.push_back(sn);
            structDefs[sn->name] = StructDef{llvm::StructType::create(context, sn->name), {}};
        }
    }
    for (auto* sn : structDecls)
    {
        auto& def = structDefs[sn->name];
        def.isUnion = sn->isUnion;
        def.alignBytes = sn->alignBytes;
        for (auto& m : sn->members)
        {
            if (m->type == ASTNodeType::VARIABLE_DECL)
            {
                auto* field = dynamic_cast<VariableDeclNode*>(m.get());
                if (field->type)
                {
                    def.fieldNames.push_back(field->name);
                    def.fieldBits.push_back(field->bitWidth);
                }
            }
        }
    }
    // 第二遍：按字段类型填充（此时所有结构体类型已存在，支持嵌套/前向引用）
    for (auto* sn : structDecls)
    {
        auto& def = structDefs[sn->name];
        std::vector<llvm::Type*> fieldTypes;
        for (auto& m : sn->members)
        {
            if (m->type == ASTNodeType::VARIABLE_DECL)
            {
                auto* field = dynamic_cast<VariableDeclNode*>(m.get());
                llvm::Type* ft = field->type ? getLLVMType(field->type.get()) : llvm::Type::getInt32Ty(context);
                def.fieldTypes.push_back(ft);
                fieldTypes.push_back(ft);
            }
        }
        if (def.isUnion)
        {
            // union：单一存储 = 最大的字段类型（共享内存，对齐取最大字段）
            llvm::Type* maxTy = llvm::Type::getInt8Ty(context);
            unsigned maxBytes = 0;
            for (auto* ft : def.fieldTypes)
            {
                unsigned b = (unsigned)typeSizeBytes(ft);
                if (b > maxBytes) { maxBytes = b; maxTy = ft; }
            }
            def.type->setBody({maxTy});
        }
        else
        {
            def.type->setBody(fieldTypes);
        }
    }

    // 先收集所有函数定义（不含 main，main 单独处理保证入口）
    for (auto& decl : root->decls)
    {
        if (decl->type == ASTNodeType::EXTERN_VAR_DECL)
        {
            // extern 全局数据：extern var stdin : ptr; → 外部全局声明
            auto* ev = dynamic_cast<ExternVarDeclNode*>(decl.get());
            if (!externGlobals.count(ev->name) && !module->getNamedGlobal(ev->name))
            {
                // 平台符号映射：macOS 的标准流符号是 __stdinp/__stdoutp/__stderrp
                std::string symbolName = ev->name;
#if defined(__APPLE__)
                if (symbolName == "stdin") symbolName = "__stdinp";
                else if (symbolName == "stdout") symbolName = "__stdoutp";
                else if (symbolName == "stderr") symbolName = "__stderrp";
#endif
                llvm::Type* ty = getLLVMType(ev->type.get());
                externGlobals[ev->name] = new llvm::GlobalVariable(
                    *module, ty, false, llvm::GlobalValue::ExternalLinkage, nullptr, symbolName);
            }
            continue;
        }
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
                    llvm::FunctionType* ft = llvm::FunctionType::get(retType, paramTypes, fn->isVariadic);
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
    labelBlocks.clear();
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", mainFunc);
    builder.SetInsertPoint(entryBB);
    for (auto& decl : root->decls)
    {
        if (decl->type == ASTNodeType::FUNCTION_DECL)
        {
            auto* fn = dynamic_cast<FunctionDeclNode*>(decl.get());
            if (fn->name == "main" && fn->body) collectLabelBlocks(fn->body.get(), mainFunc);
        }
    }

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

// 调用点参数类型转换（C 式隐式转换）
llvm::Value* CodeGenerator::coerceValue(llvm::Value* val, llvm::Type* targetTy)
{
    if (!val || val->getType() == targetTy) return val;
    llvm::Type* srcTy = val->getType();
    if (srcTy->isIntegerTy() && targetTy->isIntegerTy())
        return builder.CreateIntCast(val, targetTy, true, "argCast");
    if (srcTy->isIntegerTy() && targetTy->isFloatingPointTy())
        return builder.CreateSIToFP(val, targetTy, "argCast");
    if (srcTy->isFloatingPointTy() && targetTy->isFloatingPointTy())
        return builder.CreateFPCast(val, targetTy, "argCast");
    if (srcTy->isFloatingPointTy() && targetTy->isIntegerTy())
        return builder.CreateFPToSI(val, targetTy, "argCast");
    return val;
}

// 指针操作数的元素类型（变量已知指向类型时返回之，否则按 i8 字节寻址）
llvm::Type* CodeGenerator::pointerElementType(ASTNode* operandNode)
{
    if (operandNode && operandNode->type == ASTNodeType::VARIABLE_REF)
    {
        auto* ref = static_cast<VariableRefNode*>(operandNode);
        auto it = namedValueElementTypes.find(ref->name);
        if (it != namedValueElementTypes.end()) return it->second;
    }
    if (operandNode && operandNode->type == ASTNodeType::BINARY_OP)
    {
        // 指针算术表达式 p + n：取指针侧操作数的元素类型
        auto* bin = static_cast<BinaryOpNode*>(operandNode);
        llvm::Type* lt = pointerElementType(bin->lift.get());
        llvm::Type* rt = pointerElementType(bin->right.get());
        return lt->getPrimitiveSizeInBits() >= rt->getPrimitiveSizeInBits() ? lt : rt;
    }
    return llvm::Type::getInt8Ty(context);
}

// 递归填充初始化列表 {1, 2, {3, 4}}（结构体按字段、数组按元素，支持嵌套）
void CodeGenerator::fillInitList(llvm::Value* ptr, llvm::Type* targetTy, BlockStmtNode* block)
{
    if (targetTy->isStructTy())
    {
        std::string sname = structNameOf(targetTy);
        if (sname.empty()) return;
        auto& def = structDefs[sname];
        size_t n = std::min(block->statements.size(), def.fieldNames.size());
        for (size_t i = 0; i < n; ++i)
        {
            llvm::Type* fty = nullptr;
            int fbw = 0;
            llvm::Value* fp = getStructFieldPtr(ptr, sname, def.fieldNames[i], fty, fbw);
            auto& elem = block->statements[i];
            if (fp && fty && elem->type == ASTNodeType::BLOCK_STMT &&
                (fty->isStructTy() || fty->isArrayTy()))
            {
                fillInitList(fp, fty, static_cast<BlockStmtNode*>(elem.get()));
            }
            else if (fp && fty)
            {
                llvm::Value* v = generateExpression(elem.get());
                if (v && v->getType() != fty && fty->isIntegerTy() && v->getType()->isIntegerTy())
                    v = builder.CreateIntCast(v, fty, true, "fieldCast");
                if (v) builder.CreateStore(v, fp);
            }
        }
    }
    else if (targetTy->isArrayTy())
    {
        llvm::Type* elemTy = targetTy->getArrayElementType();
        size_t n = std::min(block->statements.size(), (size_t)targetTy->getArrayNumElements());
        for (size_t i = 0; i < n; ++i)
        {
            auto& elem = block->statements[i];
            llvm::Value* ep = builder.CreateGEP(targetTy, ptr,
                {builder.getInt64(0), builder.getInt64((int64_t)i)}, "elemPtr");
            if (elem->type == ASTNodeType::BLOCK_STMT &&
                (elemTy->isStructTy() || elemTy->isArrayTy()))
            {
                fillInitList(ep, elemTy, static_cast<BlockStmtNode*>(elem.get()));
            }
            else
            {
                llvm::Value* v = generateExpression(elem.get());
                if (v && v->getType() != elemTy && elemTy->isIntegerTy() && v->getType()->isIntegerTy())
                    v = builder.CreateIntCast(v, elemTy, true, "elemCast");
                if (v) builder.CreateStore(v, ep);
            }
        }
    }
}

// 多级成员地址解析 a.b.c：逐级解析，输出最终字段类型与位宽
llvm::Value* CodeGenerator::getMemberAddress(const std::string& dottedName, llvm::Type*& fieldType, int& bitWidth)
{
    fieldType = nullptr;
    bitWidth = 0;
    size_t first = dottedName.find('.');
    if (first == std::string::npos) return nullptr;
    std::string root = dottedName.substr(0, first);
    auto it = namedValues.find(root);
    if (it == namedValues.end() || !it->second.type->isStructTy()) return nullptr;

    llvm::Value* ptr = it->second.ptr;
    llvm::Type* curTy = it->second.type;
    std::string rest = dottedName.substr(first + 1);
    while (!rest.empty())
    {
        size_t next = rest.find('.');
        std::string member = (next == std::string::npos) ? rest : rest.substr(0, next);
        rest = (next == std::string::npos) ? "" : rest.substr(next + 1);
        std::string sname = structNameOf(curTy);
        llvm::Type* fty = nullptr;
        int fbw = 0;
        ptr = getStructFieldPtr(ptr, sname, member, fty, fbw);
        if (!ptr || !fty) return nullptr;
        curTy = fty;
        bitWidth = fbw;
    }
    fieldType = curTy;
    return ptr;
}

// 由 LLVM 结构体类型反查注册表名字
std::string CodeGenerator::structNameOf(llvm::Type* structType)
{
    for (auto& [name, def] : structDefs)
    {
        if (def.type == structType) return name;
    }
    return "";
}

// 类型大小（字节）实现
static unsigned typeSizeBytes(llvm::Type* ty)
{
    if (ty->isIntegerTy()) return ty->getIntegerBitWidth() / 8;
    if (ty->isFloatTy()) return 4;
    if (ty->isDoubleTy()) return 8;
    if (ty->isPointerTy()) return 8;
    if (ty->isArrayTy()) return typeSizeBytes(ty->getArrayElementType()) * ty->getArrayNumElements();
    return 8;
}

// 取结构体字段地址（GEP/union bitcast），并输出字段类型与位宽
llvm::Value* CodeGenerator::getStructFieldPtr(llvm::Value* structPtr, const std::string& structName,
                                              const std::string& fieldName, llvm::Type*& fieldType, int& bitWidth)
{
    fieldType = nullptr;
    bitWidth = 0;
    auto it = structDefs.find(structName);
    if (it == structDefs.end()) return nullptr;
    for (size_t i = 0; i < it->second.fieldNames.size(); ++i)
    {
        if (it->second.fieldNames[i] == fieldName)
        {
            bitWidth = (i < it->second.fieldBits.size()) ? it->second.fieldBits[i] : 0;
            fieldType = it->second.fieldTypes[i];
            if (it->second.isUnion)
            {
                // union：所有字段偏移 0，opaque ptr 下直接用字段类型读写
                return structPtr;
            }
            return builder.CreateGEP(it->second.type, structPtr,
                                     {builder.getInt32(0), builder.getInt32((int)i)}, fieldName);
        }
    }
    return nullptr;
}

// 数组/指针下标 buf[i]：计算元素地址并输出元素类型
llvm::Value* CodeGenerator::getIndexedAddress(IndexNode* node, llvm::Type*& elemType)
{
    elemType = nullptr;
    if (node->operand->type != ASTNodeType::VARIABLE_REF) {
        return nullptr;
    }
    auto* ref = static_cast<VariableRefNode*>(node->operand.get());
    // 结构体数组成员 s.arr[i]
    size_t mdot = ref->name.find('.');
    if (mdot != std::string::npos)
    {
        llvm::Type* fty = nullptr;
        int fbw = 0;
        llvm::Value* fp = getMemberAddress(ref->name, fty, fbw);
        if (fp && fty && fty->isArrayTy())
        {
            llvm::Value* idx = generateExpression(node->index.get());
            if (!idx) return nullptr;
            idx = builder.CreateIntCast(idx, llvm::Type::getInt64Ty(context), true, "idx");
            elemType = fty->getArrayElementType();
            return builder.CreateGEP(fty, fp, {builder.getInt64(0), idx}, "elemPtr");
        }
        return nullptr;
    }
    auto it = namedValues.find(ref->name);
    if (it == namedValues.end()) {
        return nullptr;
    }
    llvm::Value* idx = generateExpression(node->index.get());
    if (!idx) return nullptr;
    idx = builder.CreateIntCast(idx, llvm::Type::getInt64Ty(context), true, "idx");

    if (it->second.type->isArrayTy()) {
        // 数组变量：GEP 从 alloca 起 [0][i]
        elemType = it->second.type->getArrayElementType();
        return builder.CreateGEP(it->second.type, it->second.ptr,
                                 {builder.getInt64(0), idx}, "elemPtr");
    }
    // 指针变量/参数：先取指针值，再 GEP [i]
    auto eIt = namedValueElementTypes.find(ref->name);
    if (eIt != namedValueElementTypes.end()) {
        elemType = eIt->second;
        llvm::Value* base = builder.CreateLoad(it->second.type, it->second.ptr, ref->name);
        return builder.CreateGEP(elemType, base, {idx}, "elemPtr");
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
        if (call->arguments.size() < 1 || call->arguments.size() > 2)
        {
            std::cerr << "Error: thread.spawn expects 1 or 2 arguments" << std::endl;
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
        // 共享内存线程：把参数转发给目标函数
        if (target->arg_size() > 0)
        {
            builder.CreateCall(target, {tramp->arg_begin()});
        }
        else
        {
            builder.CreateCall(target, {});
        }
        builder.CreateRet(nullPtr);

        builder.SetInsertPoint(savedBlock, savedPoint);

        // 共享指针（第二参数）作为 pthread_create 的 arg
        llvm::Value* sharedArg = nullPtr;
        if (call->arguments.size() == 2)
        {
            sharedArg = generateExpression(call->arguments[1].get());
            if (!sharedArg) sharedArg = nullPtr;
        }
        // pthread_create(&handle, nullptr, trampoline, sharedArg)
        llvm::Function* pthreadCreate = getOrDeclareFunction(
            "pthread_create", llvm::Type::getInt32Ty(context), {ptrTy, ptrTy, ptrTy, ptrTy});
        llvm::AllocaInst* handle = builder.CreateAlloca(ptrTy, nullptr, "threadHandle");
        builder.CreateCall(pthreadCreate, {handle, nullPtr, builder.CreateBitCast(tramp, ptrTy), sharedArg});
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

// 内存序参数 → LLVM AtomicOrdering
llvm::AtomicOrdering CodeGenerator::atomicOrderOf(llvm::Value* orderVal)
{
    int o = 4;
    if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(orderVal)) o = (int)ci->getSExtValue();
    switch (o)
    {
        case 0: return llvm::AtomicOrdering::Monotonic;
        case 1: return llvm::AtomicOrdering::Acquire;
        case 2: return llvm::AtomicOrdering::Release;
        case 3: return llvm::AtomicOrdering::AcquireRelease;
        default: return llvm::AtomicOrdering::SequentiallyConsistent;
    }
}

// std.atomic 原子内置调用（LLVM atomicrmw / cmpxchg / 原子 load/store）
llvm::Value* CodeGenerator::generateAtomicBuiltin(FunctionCallNode* call)
{
    const std::string& name = call->name;
    llvm::Type* elemTy = pointerElementType(call->arguments[0].get());
    if (!elemTy->isIntegerTy())
    {
        std::cerr << "Error: atomic operations require an integer pointer" << std::endl;
        return nullptr;
    }
    const size_t n = call->arguments.size();

    // 可选内存序参数（最后一个）：0-4，默认 seq_cst
    llvm::AtomicOrdering ord = llvm::AtomicOrdering::SequentiallyConsistent;
    size_t orderIdx = n - 1;
    bool hasOrder = false;
    if ((name == "atomic.store" || name == "atomic.add" || name == "atomic.sub" ||
         name == "atomic.exchange") && n == 3) hasOrder = true;
    else if (name == "atomic.load" && n == 2) hasOrder = true;
    else if (name == "atomic.cas" && n == 4) hasOrder = true;
    if (hasOrder) ord = atomicOrderOf(generateExpression(call->arguments[orderIdx].get()));

    llvm::Value* ptr = generateExpression(call->arguments[0].get());
    if (!ptr) return nullptr;

    if (name == "atomic.load")
    {
        llvm::LoadInst* li = builder.CreateLoad(elemTy, ptr, "atomicLoad");
        li->setAtomic(ord);
        return li;
    }
    if (name == "atomic.store")
    {
        llvm::Value* v = generateExpression(call->arguments[1].get());
        v = coerceValue(v, elemTy);
        llvm::StoreInst* si = builder.CreateStore(v, ptr);
        si->setAtomic(ord);
        return nullptr;
    }
    if (name == "atomic.add" || name == "atomic.sub")
    {
        llvm::Value* v = generateExpression(call->arguments[1].get());
        v = coerceValue(v, elemTy);
        auto rmw = (name == "atomic.add") ? llvm::AtomicRMWInst::Add : llvm::AtomicRMWInst::Sub;
        return builder.CreateAtomicRMW(rmw, ptr, v, llvm::MaybeAlign(), ord);
    }
    if (name == "atomic.exchange")
    {
        llvm::Value* v = generateExpression(call->arguments[1].get());
        v = coerceValue(v, elemTy);
        return builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, v, llvm::MaybeAlign(), ord);
    }
    if (name == "atomic.cas")
    {
        llvm::Value* cmp = generateExpression(call->arguments[1].get());
        llvm::Value* newv = generateExpression(call->arguments[2].get());
        cmp = coerceValue(cmp, elemTy);
        newv = coerceValue(newv, elemTy);
        llvm::AtomicCmpXchgInst* cx = builder.CreateAtomicCmpXchg(ptr, cmp, newv, llvm::MaybeAlign(), ord, ord);
        // cmpxchg 返回 {旧值, 成功标志}，成功标志在索引 1
        return builder.CreateExtractValue(cx, 1, "casOk");
    }
    std::cerr << "Error: unknown std.atomic builtin '" << name << "'" << std::endl;
    return nullptr;
}
