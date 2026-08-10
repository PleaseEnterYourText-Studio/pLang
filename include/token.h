#ifndef TOKEN_H
#define TOKEN_H

#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <memory>
#include <unordered_map>

enum class TokenType 
{
    //字面量
    IDENT, NUMBER, STRING, CHAR_LIT,

    //关键字
    PACKAGE, IMPORT,
    VAR, VAL, MOVE,
    FUNC, IMPL, RETURN,
    USING, STRUCT, ABSTRACT,
    PUB, PRT, PRI,
    THIS, THIS_TYPE, TYPE,
    AS,
    IF, ELSE, WHILE, FOR, DO,

    //内置类型名
    INT, CHAR, STRING_TYPE, WCHAR, WSTRING, BOOL,
    I32, I16, I64, I8,
    U32, UINT, U16, U64, U8,
    F32, F64,
    TRUE, FALSE,

    //运算符
    ASSIGN,          // =
    PLUS, MINUS,     // + -
    STAR, SLASH, PERCENT,   // * / %
    EQ, NE,          // == !=
    LT, LE, GT, GE,  // < <= > >=
    AND, OR, NOT,    // && || !
    AMP, PIPE, CARET, TILDE, SHL, SHR,  // & | ^ ~ << >>
    PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN, PERCENT_ASSIGN, // += -= *= /= %=
    SHL_ASSIGN, SHR_ASSIGN, AMP_ASSIGN, PIPE_ASSIGN, CARET_ASSIGN,        // <<= >>= &= |= ^=
    INC, DEC,        // ++ --
    ARROW,           // ->
    AT,              // @
    COLON, COMMA, DOT,  // : , .
    LPAREN, RPAREN,  // ( )
    LBRACE, RBRACE,  // { }
    LBRACKET, RBRACKET, // [ ]
    SEMICOLON,       // ;

    ERROR, EOF_TOKEN
};

class Token
{
public:
    TokenType type;
    std::string text;
    int line;
    int column;

    Token(): type(TokenType::EOF_TOKEN), text(""), line(0), column(0) {};
    Token(TokenType type, std::string text, int line, int column): type(type), text(text), line(line), column(column) {};

    std::string toString() const 
    {
        static const std::unordered_map<TokenType, std::string> names = 
        {
            {TokenType::IDENT, "IDENT"}, {TokenType::NUMBER, "NUMBER"}, {TokenType::STRING, "STRING"}, {TokenType::CHAR_LIT, "CHAR_LIT"},
            {TokenType::PACKAGE, "PACKAGE"}, {TokenType::IMPORT, "IMPORT"},
            {TokenType::VAR, "VAR"}, {TokenType::VAL, "VAL"}, {TokenType::MOVE, "MOVE"},
            {TokenType::FUNC, "FUNC"}, {TokenType::IMPL, "IMPL"}, {TokenType::RETURN, "RETURN"},
            {TokenType::USING, "USING"}, {TokenType::STRUCT, "STRUCT"}, {TokenType::ABSTRACT, "ABSTRACT"},
            {TokenType::PUB, "PUB"}, {TokenType::PRT, "PRT"}, {TokenType::PRI, "PRI"},
            {TokenType::THIS, "THIS"}, {TokenType::THIS_TYPE, "THIS_TYPE"}, {TokenType::TYPE, "TYPE"},
            {TokenType::AS, "AS"},
            {TokenType::IF, "IF"}, {TokenType::ELSE, "ELSE"}, {TokenType::WHILE, "WHILE"}, {TokenType::FOR, "FOR"}, {TokenType::DO, "DO"},
            {TokenType::INT, "INT"}, {TokenType::CHAR, "CHAR"}, {TokenType::STRING_TYPE, "STRING_TYPE"}, {TokenType::WCHAR, "WCHAR"}, {TokenType::WSTRING, "WSTRING"}, {TokenType::BOOL, "BOOL"},
            {TokenType::TRUE, "TRUE"}, {TokenType::FALSE, "FALSE"},
            {TokenType::I32, "I32"}, {TokenType::I16, "I16"}, {TokenType::I64, "I64"}, {TokenType::I8, "I8"},
            {TokenType::U32, "U32"}, {TokenType::UINT, "UINT"}, {TokenType::U16, "U16"}, {TokenType::U64, "U64"}, {TokenType::U8, "U8"},
            {TokenType::F32, "F32"}, {TokenType::F64, "F64"},
            {TokenType::ASSIGN, "ASSIGN"}, {TokenType::PLUS, "PLUS"}, {TokenType::MINUS, "MINUS"},
            {TokenType::STAR, "STAR"}, {TokenType::SLASH, "SLASH"}, {TokenType::PERCENT, "PERCENT"},
            {TokenType::EQ, "EQ"}, {TokenType::NE, "NE"},
            {TokenType::LT, "LT"}, {TokenType::LE, "LE"}, {TokenType::GT, "GT"}, {TokenType::GE, "GE"},
            {TokenType::AND, "AND"}, {TokenType::OR, "OR"}, {TokenType::NOT, "NOT"},
            {TokenType::AMP, "AMP"}, {TokenType::PIPE, "PIPE"}, {TokenType::CARET, "CARET"}, {TokenType::TILDE, "TILDE"}, {TokenType::SHL, "SHL"}, {TokenType::SHR, "SHR"},
            {TokenType::PLUS_ASSIGN, "PLUS_ASSIGN"}, {TokenType::MINUS_ASSIGN, "MINUS_ASSIGN"}, {TokenType::STAR_ASSIGN, "STAR_ASSIGN"}, {TokenType::SLASH_ASSIGN, "SLASH_ASSIGN"}, {TokenType::PERCENT_ASSIGN, "PERCENT_ASSIGN"},
            {TokenType::SHL_ASSIGN, "SHL_ASSIGN"}, {TokenType::SHR_ASSIGN, "SHR_ASSIGN"}, {TokenType::AMP_ASSIGN, "AMP_ASSIGN"}, {TokenType::PIPE_ASSIGN, "PIPE_ASSIGN"}, {TokenType::CARET_ASSIGN, "CARET_ASSIGN"},
            {TokenType::INC, "INC"}, {TokenType::DEC, "DEC"}, {TokenType::ARROW, "ARROW"}, {TokenType::AT, "AT"},
            {TokenType::COLON, "COLON"}, {TokenType::COMMA, "COMMA"}, {TokenType::DOT, "DOT"},
            {TokenType::LPAREN, "LPAREN"}, {TokenType::RPAREN, "RPAREN"},
            {TokenType::LBRACE, "LBRACE"}, {TokenType::RBRACE, "RBRACE"},
            {TokenType::LBRACKET, "LBRACKET"}, {TokenType::RBRACKET, "RBRACKET"},
            {TokenType::SEMICOLON, "SEMICOLON"}, {TokenType::ERROR, "ERROR"}, {TokenType::EOF_TOKEN, "EOF_TOKEN"}
        };
        auto it = names.find(type);
        std::string typeName = (it != names.end() ) ? it->second : "UNKNOWN";
        return "Token(" + typeName + ", '" + text + "', " + 
               std::to_string(line) + ":" + std::to_string(column) + ")";
    };
};

#endif
