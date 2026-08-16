#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <memory>
#include <unordered_map>
#include "../include/token.h"
#include "../include/Lexer.h"

Lexer::Lexer(const std::string& src) : source(src), pos(0), line(1), column(1)
{
    initKeywords();
}

void Lexer::initKeywords()
{
    keywords = {
        {"package", TokenType::PACKAGE},
        {"import", TokenType::IMPORT},
        {"var", TokenType::VAR},
        {"val", TokenType::VAL},
        {"moved", TokenType::MOVE},
        {"func", TokenType::FUNC},
        {"impl", TokenType::IMPL},
        {"return", TokenType::RETURN},
        {"using", TokenType::USING},
        {"struct", TokenType::STRUCT},
        {"abstract", TokenType::ABSTRACT},
        {"pub", TokenType::PUB},
        {"prt", TokenType::PRT},
        {"pri", TokenType::PRI},
        {"this", TokenType::THIS},
        {"thisType", TokenType::THIS_TYPE},
        {"type", TokenType::TYPE},
        {"as", TokenType::AS},
        {"asm", TokenType::ASM},
        {"extern", TokenType::EXTERN},
        {"null", TokenType::NULL_LIT},
        {"if", TokenType::IF},
        {"else", TokenType::ELSE},
        {"while", TokenType::WHILE},
        {"for", TokenType::FOR},
        {"do", TokenType::DO},
        {"int", TokenType::INT},
        {"char", TokenType::CHAR},
        {"string", TokenType::STRING_TYPE},
        {"wchar", TokenType::WCHAR},
        {"wstring", TokenType::WSTRING},
        {"bool", TokenType::BOOL},
        {"true", TokenType::TRUE},
        {"false", TokenType::FALSE},
        {"i32", TokenType::I32},
        {"i16", TokenType::I16},
        {"i64", TokenType::I64},
        {"i8", TokenType::I8},
        {"u32", TokenType::U32},
        {"uint", TokenType::UINT},
        {"u16", TokenType::U16},
        {"u64", TokenType::U64},
        {"u8", TokenType::U8},
        {"f32", TokenType::F32},
        {"f64", TokenType::F64},
    };
}

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
        else if (c == '\'')
        {
            scanChar();
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

    if (peek() == '0' && (peekNext() == 'x' || peekNext() == 'X'))
    {
        advance();
        advance();
        column += 2;
        while (std::isxdigit(peek()))
        {
            advance();
            column++;
        }
        std::string text = source.substr(start, pos - start);
        int col = column - (pos - start);
        tokens.emplace_back(TokenType::NUMBER, text, line, col);
        return;
    }

    if (peek() == '0' && (peekNext() == 'b' || peekNext() == 'B'))
    {
        advance();
        advance();
        column += 2;
        while (peek() == '0' || peek() == '1')
        {
            advance();
            column++;
        }
        std::string text = source.substr(start, pos - start);
        int col = column - (pos - start);
        tokens.emplace_back(TokenType::NUMBER, text, line, col);
        return;
    }

    if (peek() == '0' && (peekNext() == 'o' || peekNext() == 'O'))
    {
        advance();
        advance();
        column += 2;
        while (peek() >= '0' && peek() <= '7')
        {
            advance();
            column++;
        }
        std::string text = source.substr(start, pos - start);
        int col = column - (pos - start);
        tokens.emplace_back(TokenType::NUMBER, text, line, col);
        return;
    }

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

    while (std::isalpha(peek()))
    {
        advance();
        column++;
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

void Lexer::scanChar()
{
    advance();
    column++;
    size_t start = pos;
    int col = column;

    if (peek() == '\\')
    {
        advance();
        column++;
        if (isAtEnd())
        {
            tokens.emplace_back(TokenType::ERROR, "Unterminated char", line, col);
            return;
        }
        advance();
        column++;
    }
    else
    {
        if (isAtEnd() || peek() == '\'')
        {
            tokens.emplace_back(TokenType::ERROR, "Empty char", line, col);
            return;
        }
        advance();
        column++;
    }

    if (peek() != '\'')
    {
        tokens.emplace_back(TokenType::ERROR, "Unterminated char", line, col);
        return;
    }
    advance();
    column++;

    std::string text = source.substr(start, pos - start - 1);
    tokens.emplace_back(TokenType::CHAR_LIT, text, line, col);
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
        case '[': addToken(TokenType::LBRACKET, "["); break;
        case ']': addToken(TokenType::RBRACKET, "]"); break;
        case ';': addToken(TokenType::SEMICOLON, ";"); break;
        case ',': addToken(TokenType::COMMA, ","); break;
        case '.': addToken(TokenType::DOT, "."); break;
        case ':': addToken(TokenType::COLON, ":"); break;
        case '@': addToken(TokenType::AT, "@"); break;
        case '~': addToken(TokenType::TILDE, "~"); break;

        case '+':
            if (match('+')) addToken(TokenType::INC, "++");
            else if (match('=')) addToken(TokenType::PLUS_ASSIGN, "+=");
            else addToken(TokenType::PLUS, "+");
            break;
        case '-':
            if (match('-')) addToken(TokenType::DEC, "--");
            else if (match('=')) addToken(TokenType::MINUS_ASSIGN, "-=");
            else if (match('>')) addToken(TokenType::ARROW, "->");
            else addToken(TokenType::MINUS, "-");
            break;
        case '*':
            if (match('=')) addToken(TokenType::STAR_ASSIGN, "*=");
            else addToken(TokenType::STAR, "*");
            break;
        case '/':
            if (peek() == '/')
            {
                handleLineComment();
            }
            else if (peek() == '*')
            {
                handleBlockComment();
            }
            else if (match('='))
            {
                addToken(TokenType::SLASH_ASSIGN, "/=");
            }
            else
            {
                addToken(TokenType::SLASH, "/");
            }
            break;
        case '%':
            if (match('=')) addToken(TokenType::PERCENT_ASSIGN, "%=");
            else addToken(TokenType::PERCENT, "%");
            break;
        case '=':
            if (match('=')) addToken(TokenType::EQ, "==");
            else addToken(TokenType::ASSIGN, "=");
            break;
        case '!':
            if (match('=')) addToken(TokenType::NE, "!=");
            else addToken(TokenType::NOT, "!");
            break;
        case '<':
            if (match('=')) addToken(TokenType::LE, "<=");
            else if (match('<'))
            {
                if (match('=')) addToken(TokenType::SHL_ASSIGN, "<<=");
                else addToken(TokenType::SHL, "<<");
            }
            else addToken(TokenType::LT, "<");
            break;
        case '>':
            if (match('=')) addToken(TokenType::GE, ">=");
            else if (match('>'))
            {
                if (match('=')) addToken(TokenType::SHR_ASSIGN, ">>=");
                else addToken(TokenType::SHR, ">>");
            }
            else addToken(TokenType::GT, ">");
            break;
        case '&':
            if (match('&')) addToken(TokenType::AND, "&&");
            else if (match('=')) addToken(TokenType::AMP_ASSIGN, "&=");
            else addToken(TokenType::AMP, "&");
            break;
        case '|':
            if (match('|')) addToken(TokenType::OR, "||");
            else if (match('=')) addToken(TokenType::PIPE_ASSIGN, "|=");
            else addToken(TokenType::PIPE, "|");
            break;
        case '^':
            if (match('=')) addToken(TokenType::CARET_ASSIGN, "^=");
            else addToken(TokenType::CARET, "^");
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
