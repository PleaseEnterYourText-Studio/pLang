#include <iostream>
#include <string>
#include "../include/Lexer.h"

int main() {
    std::string code = R"(
        int main() {
            int a = 10;
            int b = a + 20;
            if (a >= b) {
                return a;
            }
            return 0;
        }
    )";
    
    Lexer lexer(code);
    auto tokens = lexer.scanTokens();
    
    // 打印所有Token
    for (const auto& token : tokens) {
        std::cout << token.toString() << std::endl;
    }
    
    return 0;
}