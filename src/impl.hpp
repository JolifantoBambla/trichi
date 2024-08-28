//
// Created by lukas on 28.08.24.
//

#ifndef PER_MESHLET_NUANCES_IMPL_HPP
#define PER_MESHLET_NUANCES_IMPL_HPP

#include "meshoptimizer.h"

#ifdef PMN_PARALLEL
#include "BS_thread_pool.hpp"
#endif

#include "util.hpp"

namespace pmn {
struct MeshletsBuffers {
  std::vector<meshopt_Meshlet> meshlets;
  std::vector<unsigned int> vertices;
  std::vector<unsigned char> triangles;
};

struct Cluster {
  size_t index;
  const MeshletsBuffers* buffers;

  [[nodiscard]] const meshopt_Meshlet& getMeshlet() const { return buffers->meshlets[index]; }
};

[[nodiscard]] std::unordered_map<uint64_t, int> extract_cluster_edges(const Cluster& cluster);

void extract_boundary(const Cluster& cluster, std::vector<uint64_t>& boundary);

#ifdef PMN_PARALLEL
[[nodiscard]] std::vector<std::vector<uint64_t>> extract_boundaries(
    const std::vector<Cluster>& clusters,
    BS::thread_pool& threadPool);
#else
[[nodiscard]] std::vector<std::vector<uint64_t>> extract_boundaries(const std::vector<Cluster>& clusters);
#endif

[[nodiscard]] std::vector<std::vector<size_t>> group_clusters(
    const std::vector<Cluster>& clusters,
    size_t max_clusters_per_group);
}  // namespace pmn

#endif  //PER_MESHLET_NUANCES_IMPL_HPP
