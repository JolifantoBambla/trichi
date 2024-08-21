#include <iostream>

#include "meshoptimizer.h"
#include "metis.h"

int main() {
    std::cout << "Hello, World! " << meshopt_buildMeshletsBound(2001, 255, 512) << " " << METIS_DBG_COARSEN << std::endl;
    return 0;
}
