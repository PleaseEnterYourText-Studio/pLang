#include <iostream>
#include <string>
#include <vector>
#include "Lexer.h"
#include "token.h"

int main() {
    std::string source =
        "package foo;\n"
        "import std.vector;\n"
        "using vec = std.vector<i32>;\n"
        "\n"
        "using Shape = abstract {\n"
        "    pub func area() -> f64;\n"
        "};\n"
        "using Circle = struct : pub Shape {\n"
        "    prt val: f64 r;\n"
        "    pri val: i32 secret;\n"
        "    pub func area() -> f64 {\n"
        "        return 3.14 * this.r * this.r;\n"
        "    }\n"
        "    pub func .converter();\n"
        "};\n"
        "impl Circle.converter {\n"
        "    this.secret = 42;\n"
        "}\n"
        "func foo<T: type>(val: T a) -> typeof(a) {\n"
        "    var: i32 arr[3] = {1, 2, 3};\n"
        "    var -> var: i32 p = &a;\n"
        "    var b@a;\n"
        "    if (a > 0) {\n"
        "        a += 2;\n"
        "    } else if (a == 0) {\n"
        "        a = (int as b);\n"
        "    } else {\n"
        "        a %= 3;\n"
        "    }\n"
        "    while (a < 10) { a++; }\n"
        "    do { a--; } while (a > 0);\n"
        "    for (var: i32 i = 0; i < 5; i++) { a <<= 1; }\n"
        "    return a;\n"
        "}\n"
        "func main() -> int {\n"
        "    var: char c = 'x';\n"
        "    var: string s = \"hi\\n\\t\";\n"
        "    var: u64 x = 0xFF + 0b1010 + 0o17 + 1ll;\n"
        "    var: f32 f = 2.5f;\n"
        "    var: bool ok = (x >= 1 && ok) || !ok;\n"
        "    return 0;\n"
        "}\n";

    Lexer lexer(source);
    auto tokens = lexer.scanTokens();

    int errors = 0;
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::ERROR) {
            std::cout << "[ERROR TOKEN] " << tok.toString() << "\n";
            errors++;
        }
    }
    if (errors == 0) std::cout << "无错误 token\n";

    std::cout << "=== 全部 token (" << tokens.size() << ") ===\n";
    for (const auto& tok : tokens) {
        std::cout << tok.toString() << "\n";
    }
    return errors == 0 ? 0 : 1;
}
