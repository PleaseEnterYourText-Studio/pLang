#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "Lexer.h"
#include "Parser.h"
#include "AST.h"
#include "token.h"

void printAST(const ASTNode* node, int depth = 0)
{
    if (!node) return;
    std::string indent(depth * 2, ' ');
    std::cout << indent;

    switch (node->type)
    {
        case ASTNodeType::PROGRAM: std::cout << "Program"; break;
        case ASTNodeType::PACKAGE_STMT:
            std::cout << "Package: " << dynamic_cast<const PackageStmtNode*>(node)->name; break;
        case ASTNodeType::IMPORT_STMT:
            std::cout << "Import: " << dynamic_cast<const ImportStmtNode*>(node)->path; break;
        case ASTNodeType::USING_DECL:
            std::cout << "Using: " << dynamic_cast<const UsingDeclNode*>(node)->name; break;
        case ASTNodeType::FUNCTION_DECL:
            std::cout << "Func: " << dynamic_cast<const FunctionDeclNode*>(node)->name
                      << " (body=" << (dynamic_cast<const FunctionDeclNode*>(node)->hasBody ? "yes" : "no") << ")";
            break;
        case ASTNodeType::STRUCT_DECL:
        {
            auto* s = dynamic_cast<const StructDeclNode*>(node);
            std::cout << (s->isAbstract ? "Abstract: " : "Struct: ") << s->name;
            if (!s->bases.empty()) std::cout << " : " << s->bases[0];
            break;
        }
        case ASTNodeType::IMPL_DECL:
            std::cout << "Impl: " << dynamic_cast<const ImplDeclNode*>(node)->target; break;
        case ASTNodeType::VARIABLE_DECL:
            std::cout << "VarDecl: " << dynamic_cast<const VariableDeclNode*>(node)->name; break;
        case ASTNodeType::ASSIGNMENT_STMT: std::cout << "Assign"; break;
        case ASTNodeType::RETURN_STMT: std::cout << "Return"; break;
        case ASTNodeType::BLOCK_STMT: std::cout << "Block"; break;
        case ASTNodeType::IF_STMT: std::cout << "If"; break;
        case ASTNodeType::WHILE_STMT: std::cout << "While"; break;
        case ASTNodeType::FOR_STMT: std::cout << "For"; break;
        case ASTNodeType::EXPRESSION_STMT: std::cout << "ExprStmt"; break;
        case ASTNodeType::FUNCTION_CALL:
            std::cout << "Call: " << dynamic_cast<const FunctionCallNode*>(node)->name; break;
        case ASTNodeType::VARIABLE_REF:
            std::cout << "Var: " << dynamic_cast<const VariableRefNode*>(node)->name; break;
        case ASTNodeType::LITERAL_INT:
            std::cout << "Int: " << dynamic_cast<const LiteralIntNode*>(node)->value; break;
        case ASTNodeType::LITERAL_FLOAT:
            std::cout << "Float: " << dynamic_cast<const LiteralFloatNode*>(node)->value; break;
        case ASTNodeType::LITERAL_STRING:
            std::cout << "String: \"" << dynamic_cast<const LiteralStringNode*>(node)->value << "\""; break;
        case ASTNodeType::LITERAL_BOOL:
            std::cout << "Bool: " << (dynamic_cast<const LiteralBoolNode*>(node)->value ? "true" : "false"); break;
        case ASTNodeType::BINARY_OP:
        {
            auto* b = dynamic_cast<const BinaryOpNode*>(node);
            const char* opName = "?";
            switch (b->op)
            {
                case BinaryOpType::ADD: opName = "ADD"; break;
                case BinaryOpType::SUB: opName = "SUB"; break;
                case BinaryOpType::MUL: opName = "MUL"; break;
                case BinaryOpType::DIV: opName = "DIV"; break;
                case BinaryOpType::MOD: opName = "MOD"; break;
            }
            std::cout << "Binary(" << opName << ")";
            break;
        }
        case ASTNodeType::UNARY_OP:
        {
            auto* u = dynamic_cast<const UnaryOpNode*>(node);
            const char* opName = "?";
            switch (u->op)
            {
                case UnaryOpType::NEG: opName = "NEG"; break;
                case UnaryOpType::NOT: opName = "NOT"; break;
                case UnaryOpType::INC: opName = "INC"; break;
                case UnaryOpType::DEC: opName = "DEC"; break;
            }
            std::cout << "Unary(" << opName << ")";
            break;
        }
        case ASTNodeType::COMPARISON_OP:
        {
            auto* c = dynamic_cast<const ComparisonOpNode*>(node);
            const char* opName = "?";
            switch (c->op)
            {
                case ComparisonOpType::EQ: opName = "EQ"; break;
                case ComparisonOpType::NE: opName = "NE"; break;
                case ComparisonOpType::LT: opName = "LT"; break;
                case ComparisonOpType::LE: opName = "LE"; break;
                case ComparisonOpType::GT: opName = "GT"; break;
                case ComparisonOpType::GE: opName = "GE"; break;
            }
            std::cout << "Cmp(" << opName << ")";
            break;
        }
        case ASTNodeType::LOGICAL_OP:
        {
            auto* l = dynamic_cast<const LogicalOpNode*>(node);
            const char* opName = "?";
            switch (l->op)
            {
                case LogicalOpType::AND: opName = "AND"; break;
                case LogicalOpType::OR: opName = "OR"; break;
            }
            std::cout << "Logical(" << opName << ")";
            break;
        }
        default: std::cout << "Node(" << static_cast<int>(node->type) << ")"; break;
    }
    std::cout << "\n";
}

void printChildren(const ASTNode* node, int depth = 0)
{
    if (!node) return;
    printAST(node, depth);
    int d = depth + 1;

    if (node->type == ASTNodeType::PROGRAM)
    {
        auto* p = dynamic_cast<const ProgramNode*>(node);
        for (auto& d_ : p->decls) { printChildren(d_.get(), d); }
    }
    else if (node->type == ASTNodeType::FUNCTION_DECL)
    {
        auto* f = dynamic_cast<const FunctionDeclNode*>(node);
        for (auto& pa : f->params) printChildren(pa.get(), d);
        if (f->body) printChildren(f->body.get(), d);
    }
    else if (node->type == ASTNodeType::BLOCK_STMT)
    {
        auto* b = dynamic_cast<const BlockStmtNode*>(node);
        for (auto& s_ : b->statements)
        {
            printChildren(s_.get(), d);
        }
    }
    else if (node->type == ASTNodeType::STRUCT_DECL)
    {
        auto* s = dynamic_cast<const StructDeclNode*>(node);
        for (auto& m_ : s->members) printChildren(m_.get(), d);
    }
    else if (node->type == ASTNodeType::IF_STMT)
    {
        auto* i = dynamic_cast<const IfStmtNode*>(node);
        if (i->condition) printChildren(i->condition.get(), d);
        if (i->thenBranch) printChildren(i->thenBranch.get(), d);
        if (i->elseBranch) printChildren(i->elseBranch.get(), d);
    }
    else if (node->type == ASTNodeType::WHILE_STMT)
    {
        auto* w = dynamic_cast<const WhileStmtNode*>(node);
        if (w->condition) printChildren(w->condition.get(), d);
        if (w->body) printChildren(w->body.get(), d);
    }
    else if (node->type == ASTNodeType::FOR_STMT)
    {
        auto* f = dynamic_cast<const ForStmtNode*>(node);
        if (f->init) printChildren(f->init.get(), d);
        if (f->condition) printChildren(f->condition.get(), d);
        if (f->update) printChildren(f->update.get(), d);
        if (f->body) printChildren(f->body.get(), d);
    }
    else if (node->type == ASTNodeType::VARIABLE_DECL)
    {
        auto* v = dynamic_cast<const VariableDeclNode*>(node);
        if (v->initializer) printChildren(v->initializer.get(), d);
    }
    else if (node->type == ASTNodeType::RETURN_STMT)
    {
        auto* r = dynamic_cast<const ReturnStmtNode*>(node);
        if (r->value) printChildren(r->value.get(), d);
    }
    else if (node->type == ASTNodeType::ASSIGNMENT_STMT)
    {
        auto* a = dynamic_cast<const AssignmentNode*>(node);
        if (a->target) printChildren(a->target.get(), d);
        if (a->value) printChildren(a->value.get(), d);
    }
    else if (node->type == ASTNodeType::EXPRESSION_STMT)
    {
        auto* e = dynamic_cast<const ExpressionStmtNode*>(node);
        if (e->expr) printChildren(e->expr.get(), d);
    }
    else if (node->type == ASTNodeType::FUNCTION_CALL)
    {
        auto* c = dynamic_cast<const FunctionCallNode*>(node);
        for (auto& a_ : c->arguments) printChildren(a_.get(), d);
    }
    else if (node->type == ASTNodeType::BINARY_OP)
    {
        auto* b = dynamic_cast<const BinaryOpNode*>(node);
        printChildren(b->lift.get(), d);
        printChildren(b->right.get(), d);
    }
    else if (node->type == ASTNodeType::COMPARISON_OP)
    {
        auto* c = dynamic_cast<const ComparisonOpNode*>(node);
        printChildren(c->lift.get(), d);
        printChildren(c->right.get(), d);
    }
    else if (node->type == ASTNodeType::LOGICAL_OP)
    {
        auto* l = dynamic_cast<const LogicalOpNode*>(node);
        printChildren(l->lift.get(), d);
        printChildren(l->right.get(), d);
    }
    else if (node->type == ASTNodeType::UNARY_OP)
    {
        auto* u = dynamic_cast<const UnaryOpNode*>(node);
        printChildren(u->operand.get(), d);
    }
    else
    {
        // leaf node, already printed
    }
}

int main() {
    std::string source =
        "package foo;\n"
        "import std.vector;\n"
        "\n"
        "func main() -> int {\n"
        "    var: int a = 1 + 2 * 3;\n"
        "    var: int b = 0xFF;\n"
        "    if (a >= 10) {\n"
        "        a = a - 5;\n"
        "    } else {\n"
        "        a += 1;\n"
        "    }\n"
        "    while (a < 10) { a++; }\n"
        "    return a;\n"
        "}\n";

    Lexer lexer(source);
    auto tokens = lexer.scanTokens();
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::ERROR) {
            std::cerr << "lexical error: " << tok.toString() << "\n";
            return 1;
        }
    }

    Parser parser(tokens);
    try {
        auto program = parser.parse();
        printAST(program.get());
        printChildren(program.get());
    } catch (const std::exception& e) {
        std::cerr << "parse error: " << e.what() << "\n";
        return 1;
    }
    std::cout << "parsing passed\n";
    return 0;
}
