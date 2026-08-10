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
    //文件标识符及字面量
    IDENT, NUMBER, STRING,

    //关键字
    IF, ELSE, WHILE, FOR, RETURM, INT, VOID, CHAR,

    //运算符
    ASSIGN ,  // =
    PLUS, MINUS, // + -
    STAR, SLASH, // * /
    EQ, NE, // == !=
    LT, LE, GT, GE, // < <= > >=

    //分隔符
    LPAREN, RPAREN, // ( )
    LBRACE, RBRACE,  // { }
    SEMICOLON,  // ;

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
        static std::unordered_map<TokenType, std::string> names = 
        {
            {TokenType::ASSIGN, "ASSIGN"},
            {TokenType::CHAR, "CHAR"},
            {TokenType::ELSE, "ELSE"},
            {TokenType::EOF_TOKEN, "EOF_TOKEN"},
            {TokenType::EQ, "EQ"},
            {TokenType::ERROR, "ERROR"},
            {TokenType::FOR, "FOR"},
            {TokenType::GE, "GE"},
            {TokenType::GT, "GT"},
            {TokenType::IDENT, "IDENT"},
            {TokenType::IF, "IF"},
            {TokenType::INT, "INT"},
            {TokenType::LBRACE, "LBRACE"},
            {TokenType::LE, "LE"},
            {TokenType::LPAREN, "LPAREN"},
            {TokenType::LT, "LT"},
            {TokenType::MINUS, "MINUS"},
            {TokenType::NE, "NE"},
            {TokenType::NUMBER, "NUMBER"},
            {TokenType::PLUS, "PLUS"},
            {TokenType::RBRACE, "RBRACE"},
            {TokenType::RETURM, "RETURM"},
            {TokenType::RPAREN, "RPAREM"}
        };
        auto it = names.find(type);
        std::string typeName = (it != names.end() ) ? it->second : "UNKNOWN";
        return "Token(" + typeName + ", '" + text + "', " + 
               std::to_string(line) + ":" + std::to_string(column) + ")";
    };


};

#endif