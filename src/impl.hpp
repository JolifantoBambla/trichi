//
// Created by lukas on 28.08.24.
//

#ifndef TRICHI_IMPL_HPP
#define TRICHI_IMPL_HPP

#include <unordered_map>
#include <vector>

#include "meshoptimizer.h"

#include "trichi.hpp"
#include "util.hpp"

namespace trichi {
struct MeshletsBuffers {
  std::vector<Cluster> clusters;
  std::vector<unsigned int> vertices;
  std::vector<unsigned char> triangles;
};

struct ClusterIndex {
  size_t index{};
  size_t lod{};
};

[[nodiscard]] std::unordered_map<uint64_t, int> extract_cluster_edges(const ClusterIndex& cluster,
                                                                      const MeshletsBuffers& lods);

void extract_boundary(const ClusterIndex& cluster,
                      const MeshletsBuffers& lods, std::vector<uint64_t>& boundary);

[[nodiscard]] std::vector<std::vector<uint64_t>> extract_boundaries(const std::vector<ClusterIndex>& clusters, const MeshletsBuffers& lods, LoopRunner& loop_runner);

[[nodiscard]] std::vector<std::vector<size_t>> group_clusters(
    const std::vector<ClusterIndex>& clusters,
    const MeshletsBuffers& lods,
    size_t max_clusters_per_group,
    LoopRunner& loop_runner);
}  // namespace trichi

#endif  //TRICHI_IMPL_HPP
