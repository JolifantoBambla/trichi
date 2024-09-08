//
// Created by lukas on 21.08.24.
//

#ifndef TRICHI_HPP
#define TRICHI_HPP

#include <cstdint>
#include <vector>

namespace trichi {
// todo: no defaults
struct TrichiParams {
  size_t max_vertices = 64;
  size_t max_triangles = 128;
  float cone_weight = 0.5;
  size_t max_num_clusters_per_group = 4;
  float simplify_target_index_count_threshold = 0.5f;
  size_t max_lod_count = 25;
  float min_target_error = 1e-2f;
  float max_target_error = 1.0f;
};

struct ErrorBounds {
  float center[3]{};
  float radius{};
  float error = 0.0;
};

struct NormalCone {
  float cone_apex[3];
  float cone_axis[3];
  float cone_cutoff;
};

struct NodeErrorBounds {
  ErrorBounds parent_error;
  ErrorBounds cluster_error;
  NormalCone normal_cone{};
};

struct Node {
  std::vector<size_t> child_indices;
};

/**
 * Shadows meshopt_Meshlet
 */
struct Cluster {
  unsigned int vertex_offset;
  unsigned int triangle_offset;
  unsigned int vertex_count;
  unsigned int triangle_count;
};

struct ClusterHierarchy {
  std::vector<Node> nodes;
  std::vector<size_t> root_nodes;
  std::vector<NodeErrorBounds> node_errors;
  std::vector<Cluster> clusters;
  std::vector<uint32_t> vertices;
  std::vector<uint8_t> triangles;
};

[[nodiscard]] ClusterHierarchy build_cluster_hierarchy(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride, const TrichiParams& params = {});
}

#endif  //TRICHI_HPP
