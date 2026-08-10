#include <llvm-c/Core.h>
#include <llvm/Config/llvm-config.h>
#include <iostream>
#include "main.h"

int main() {
    std::cout << "PLang (LLVM " << LLVM_VERSION_STRING << ")\n";
    return 0;
}
