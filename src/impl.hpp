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
  std::vector<meshopt_Meshlet> meshlets;
  std::vector<unsigned int> vertices;
  std::vector<unsigned char> triangles;
  std::vector<meshopt_Bounds> bounds;
};

struct ClusterIndex {
  size_t index{};
  size_t lod{};
};

struct ClusterBounds {
  float center[3];
  float radius;
  float cone_axis[3];
  float cone_cutoff;
  float cone_apex[3];
  float error;
};

struct DagNode {
  size_t clusterIndex = 0;
  size_t level = 0;
  std::vector<size_t> children{};
  ClusterBounds bounds{};
  ErrorBounds parent_error{};
  ErrorBounds cluster_error{};
};

[[nodiscard]] std::unordered_map<uint64_t, int> extract_cluster_edges(const ClusterIndex& cluster,
                                                                      const MeshletsBuffers& lods);

void extract_boundary(const ClusterIndex& cluster,
                      const MeshletsBuffers& lods, std::vector<uint64_t>& boundary);

void init_dag_node(
    const ClusterIndex& cluster,
    const MeshletsBuffers& buffers,
    DagNode& dagNode,
    const std::vector<float>& vertices,
    size_t vertex_count,
    size_t vertex_stride,
    size_t cluster_index,
    size_t level,
    float error);

[[nodiscard]] std::vector<std::vector<uint64_t>> extract_boundaries(const std::vector<ClusterIndex>& clusters, const MeshletsBuffers& lods, LoopRunner& loop_runner);

[[nodiscard]] std::vector<std::vector<size_t>> group_clusters(
    const std::vector<ClusterIndex>& clusters,
    const MeshletsBuffers& lods,
    size_t max_clusters_per_group,
    LoopRunner& loop_runner);
}  // namespace trichi

#endif  //TRICHI_IMPL_HPP
