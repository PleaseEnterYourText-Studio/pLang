#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <memory>
#include <unordered_map>
#include "../include/token.h"
#include "../include/Lexer.h"

Lexer::Lexer(const std::string& src) : source(src), pos(0), line(1), column(1) {}

std::vector<Token> Lexer::scanTokens()
{
    tokens.clear();
    while (!isAtEnd())
    {
        char c = peek();
        if (std::isspace(c))
        {
            advance();
            if (c == '\n')
            {
                line++;
                column = 1;
            }
            else
            {
                column++;
            }
            continue;
        }

        if (std::isalpha(c) || c == '_')
        {
            scanIdentifier();
        }
        else if (std::isdigit(c))
        {
            scanNumber();
        }
        else if (c == '"')
        {
            scanString();
        }
        else
        {
            scanSymbol();
        }
    }
    tokens.emplace_back(TokenType::EOF_TOKEN, "", line, column);
    return tokens;
}

bool Lexer::isAtEnd() const
{
    return pos >= source.length();
}

char Lexer::peek() const
{
    return isAtEnd() ? '\0' : source[pos];
}

char Lexer::peekNext() const
{
    if (pos + 1 >= source.length()) return '\0';
    return source[pos + 1];
}

char Lexer::advance()
{
    return source[pos++];
}

bool Lexer::match(char expected)
{
    if (isAtEnd()) return false;
    if (source[pos] != expected) return false;
    pos++;
    column++;
    return true;
}

void Lexer::scanIdentifier()
{
    size_t start = pos;
    while (std::isalnum(peek()) || peek() == '_')
    {
        advance();
        column++;
    }

    std::string text = source.substr(start, pos - start);
    int col = column - (pos - start);

    auto it = keywords.find(text);
    TokenType type = (it != keywords.end()) ? it->second : TokenType::IDENT;

    tokens.emplace_back(type, text, line, col);
}

void Lexer::scanNumber()
{
    size_t start = pos;
    bool isFloat = false;

    while (std::isdigit(peek()))
    {
        advance();
        column++;
    }

    if (peek() == '.' && std::isdigit(peekNext()))
    {
        isFloat = true;
        advance();
        column++;
        while (std::isdigit(peek()))
        {
            advance();
            column++;
        }
    }

    std::string text = source.substr(start, pos - start);
    int col = column - (pos - start);
    tokens.emplace_back(TokenType::NUMBER, text, line, col);
}

void Lexer::scanString()
{
    advance();
    column++;
    size_t start = pos;
    int col = column;

    while (peek() != '"' && !isAtEnd())
    {
        if (peek() == '\\')
        {
            advance();
            column++;
        }
        advance();
        column++;
    }

    if (isAtEnd())
    {
        tokens.emplace_back(TokenType::ERROR, "Unterminated string", line, col);
        return;
    }

    std::string text = source.substr(start, pos - start);
    advance();
    column++;

    tokens.emplace_back(TokenType::STRING, text, line, col);
}

void Lexer::scanSymbol()
{
    char c = advance();
    int col = column;
    column++;

    switch (c)
    {
        case '(': addToken(TokenType::LPAREN, "("); break;
        case ')': addToken(TokenType::RPAREN, ")"); break;
        case '{': addToken(TokenType::LBRACE, "{"); break;
        case '}': addToken(TokenType::RBRACE, "}"); break;
        case ';': addToken(TokenType::SEMICOLON, ";"); break;
        case '+': addToken(TokenType::PLUS, "+"); break;
        case '-': addToken(TokenType::MINUS, "-"); break;
        case '*': addToken(TokenType::STAR, "*"); break;
        case '/':
            if (peek() == '/')
            {
                handleLineComment();
            }
            else if (peek() == '*')
            {
                handleBlockComment();
            }
            else
            {
                addToken(TokenType::SLASH, "/");
            }
            break;
        case '=':
            if (match('='))
            {
                addToken(TokenType::EQ, "==");
            }
            else
            {
                addToken(TokenType::ASSIGN, "=");
            }
            break;
        case '!':
            if (match('='))
            {
                addToken(TokenType::NE, "!=");
            }
            else
            {
                addToken(TokenType::ERROR, "Unexpected '!'");
            }
            break;
        case '<':
            if (match('='))
            {
                addToken(TokenType::LE, "<=");
            }
            else
            {
                addToken(TokenType::LT, "<");
            }
            break;
        case '>':
            if (match('='))
            {
                addToken(TokenType::GE, ">=");
            }
            else
            {
                addToken(TokenType::GT, ">");
            }
            break;
        default:
            tokens.emplace_back(TokenType::ERROR, std::string(1, c), line, col);
            break;
    }
}

void Lexer::handleLineComment()
{
    while (peek() != '\n' && !isAtEnd())
    {
        advance();
        column++;
    }
}

void Lexer::handleBlockComment()
{
    advance();
    column++;
    while (!isAtEnd())
    {
        if (peek() == '*' && peekNext() == '/')
        {
            advance();
            advance();
            column += 2;
            break;
        }
        if (peek() == '\n')
        {
            line++;
            column = 1;
        }
        else
        {
            column++;
        }
        advance();
    }
}

void Lexer::addToken(TokenType type, const std::string& text)
{
    tokens.emplace_back(type, text, line, column - 1);
}