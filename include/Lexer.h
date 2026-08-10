#ifndef LEXER_H
#define LEXER_H

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include "token.h"

class Lexer
{
private:
    std::string source;
    size_t pos;
    int line;
    int column;
    std::vector<Token> tokens;

    std::unordered_map<std::string, TokenType> keywords;

public:
    Lexer(const std::string& src);

    std::vector<Token> scanTokens();

private:
    bool isAtEnd() const;

    char peek() const;

    char peekNext() const;

    char advance();

    bool match(char expected);

    void scanIdentifier();

    void scanNumber();

    void scanString();

    void scanSymbol();

    void handleLineComment();

    void handleBlockComment();

    void addToken(TokenType type, const std::string& text);
};

#endif