#include <iostream>
#include <string>
#include <vector>
#include "Lexer.h"
#include "token.h"

int main() {
    struct KeywordTest {
        const char* keyword;
        TokenType expected;
    };

    std::vector<KeywordTest> tests = {
        {"package", TokenType::PACKAGE}, {"import", TokenType::IMPORT},
        {"var", TokenType::VAR}, {"val", TokenType::VAL}, {"move", TokenType::MOVE},
        {"func", TokenType::FUNC}, {"impl", TokenType::IMPL}, {"return", TokenType::RETURN},
        {"using", TokenType::USING}, {"struct", TokenType::STRUCT}, {"abstract", TokenType::ABSTRACT},
        {"pub", TokenType::PUB}, {"prt", TokenType::PRT}, {"pri", TokenType::PRI},
        {"this", TokenType::THIS}, {"thisType", TokenType::THIS_TYPE}, {"type", TokenType::TYPE},
        {"as", TokenType::AS},
        {"if", TokenType::IF}, {"else", TokenType::ELSE}, {"while", TokenType::WHILE},
        {"for", TokenType::FOR}, {"do", TokenType::DO},
        {"int", TokenType::INT}, {"char", TokenType::CHAR}, {"string", TokenType::STRING_TYPE},
        {"wchar", TokenType::WCHAR}, {"wstring", TokenType::WSTRING},
        {"i32", TokenType::I32}, {"i16", TokenType::I16}, {"i64", TokenType::I64}, {"i8", TokenType::I8},
        {"u32", TokenType::U32}, {"uint", TokenType::UINT}, {"u16", TokenType::U16},
        {"u64", TokenType::U64}, {"u8", TokenType::U8},
        {"f32", TokenType::F32}, {"f64", TokenType::F64},
    };

    int pass = 0, fail = 0;

    std::string source;
    for (const auto& t : tests) {
        source += t.keyword;
        source += " ";
    }

    Lexer lexer(source);
    auto tokens = lexer.scanTokens();

    std::cout << "=== 关键字识别测试 ===\n";
    for (size_t i = 0; i < tests.size(); ++i) {
        const auto& tok = tokens[i];
        const auto& expect = tests[i];
        bool ok = (tok.type == expect.expected);
        std::cout << (ok ? "[PASS] " : "[FAIL] ")
                  << expect.keyword << " -> " << tok.toString() << "\n";
        ok ? pass++ : fail++;
    }

    std::cout << "\n=== 非关键字(应识别为IDENT) ===\n";
    Lexer lexer2("hello fooBar _tmp x1");
    auto tokens2 = lexer2.scanTokens();
    for (size_t i = 0; i < 4; ++i) {
        bool ok = (tokens2[i].type == TokenType::IDENT);
        std::cout << (ok ? "[PASS] " : "[FAIL] ")
                  << tokens2[i].text << " -> " << tokens2[i].toString() << "\n";
        ok ? pass++ : fail++;
    }

    std::cout << "\n结果: " << pass << " 通过, " << fail << " 失败\n";
    return fail == 0 ? 0 : 1;
}
