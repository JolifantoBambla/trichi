#include <iostream>

#include "meshoptimizer.h"

int main() {
    std::cout << "Hello, World!" << meshopt_buildMeshletsBound(2001, 255, 512) << std::endl;
    return 0;
}
