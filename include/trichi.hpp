//
// Created by lukas on 21.08.24.
//

#ifndef TRICHI_HPP
#define TRICHI_HPP

#include <vector>

namespace trichi {

void build_cluster_hierarchy(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride);
}

#endif  //TRICHI_HPP
