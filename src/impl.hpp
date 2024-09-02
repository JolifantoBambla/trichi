//
// Created by lukas on 28.08.24.
//

#ifndef PER_MESHLET_NUANCES_IMPL_HPP
#define PER_MESHLET_NUANCES_IMPL_HPP

#include <unordered_map>
#include <vector>

#include "meshoptimizer.h"

#include "util.hpp"

namespace pmn {
struct MeshletsBuffers {
  std::vector<meshopt_Meshlet> meshlets;
  std::vector<unsigned int> vertices;
  std::vector<unsigned char> triangles;
};

struct Cluster {
  size_t index{};
  size_t lod{};
  size_t dag_index = 0;
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
};

[[nodiscard]] std::unordered_map<uint64_t, int> extract_cluster_edges(const Cluster& cluster,
                                                                      const std::vector<MeshletsBuffers>& lods);

void extract_boundary(const Cluster& cluster,
                      const std::vector<MeshletsBuffers>& lods, std::vector<uint64_t>& boundary);

void init_dag_node(
    const Cluster& cluster,
    const MeshletsBuffers& buffers,
    DagNode& dagNode,
    const std::vector<float>& vertices,
    size_t vertex_count,
    size_t vertex_stride,
    size_t cluster_index,
    size_t level,
    float error);

[[nodiscard]] std::vector<std::vector<uint64_t>> extract_boundaries(const std::vector<Cluster>& clusters, const std::vector<MeshletsBuffers>& lods, LoopRunner& loop_runner);

[[nodiscard]] std::vector<std::vector<size_t>> group_clusters(
    const std::vector<Cluster>& clusters,
    const std::vector<MeshletsBuffers>& lods,
    size_t max_clusters_per_group,
    LoopRunner& loop_runner);
}  // namespace pmn

#endif  //PER_MESHLET_NUANCES_IMPL_HPP
