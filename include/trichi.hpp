//
// Created by lukas on 21.08.24.
//

#ifndef TRICHI_HPP
#define TRICHI_HPP

#include <cstdint>
#include <vector>

namespace trichi {

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
  float center[3];
  float radius;
  float error = 0.0;
};

struct NodeErrorBounds {
  ErrorBounds parent_error;
  ErrorBounds cluster_error;
};

struct Node {
  std::vector<size_t> child_indices;
};

/**
 * Shadows meshopt_Meshlet
 */
struct Cluster2 {
  unsigned int vertex_offset;
  unsigned int triangle_offset;
  unsigned int vertex_count;
  unsigned int triangle_count;
};

/**
 * Shadows meshopt_Bounds
 */
struct Cluster2Bounds {
  float center[3];
  float radius;
  float cone_apex[3];
  float cone_axis[3];
  float cone_cutoff;
  signed char cone_axis_s8[3];
  signed char cone_cutoff_s8;
};

struct ClusterHierarchy {
  std::vector<Node> nodes;
  std::vector<size_t> root_nodes;
  std::vector<NodeErrorBounds> node_errors;
  std::vector<Cluster2> clusters;
  std::vector<uint32_t> vertices;
  std::vector<uint8_t> triangles;
  std::vector<Cluster2Bounds> bounds;
};

// todo: no defaults
void build_cluster_hierarchy(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride, const TrichiParams& params = {});
}

#endif  //TRICHI_HPP
