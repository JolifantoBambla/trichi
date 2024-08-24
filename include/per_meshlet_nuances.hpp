//
// Created by lukas on 21.08.24.
//

#ifndef PER_MESHLET_NUANCES_HPP
#define PER_MESHLET_NUANCES_HPP

#include <vector>

namespace pmn {
void create_dag(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride);
}

#endif  //PER_MESHLET_NUANCES_HPP
