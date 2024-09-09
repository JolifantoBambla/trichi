/**
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

#ifndef TRICHI_HPP
#define TRICHI_HPP

#include <cstdint>
#include <vector>

namespace trichi {
/**
 * Tuning parameters for building a triangle cluster hierarchy.
 */
struct TriChiParams {
  /**
   * The maximum number of vertices per cluster.
   */
  size_t max_vertices_per_cluster = 64;

  /**
   * The maximum number of triangles per cluster.
   */
  size_t max_triangles_per_cluster = 128;

  /**
   * A weighting factor for the importance of cluster normal cones used when building the clusters.
   * In range [0..1].
   */
  float cluster_cone_weight = 0.5;

  /**
   * The target number of clusters per group.
   */
  size_t target_clusters_per_group = 4;

  /**
   * The maximum number of iterations when building the hierarchy.
   * In each iteration, the number of triangles is approximately halved.
   */
  size_t max_hierarchy_depth = 25;

  /**
   * The size of the thread pool used for parallelizing DAG building steps.
   * If `trichi` is not built with multithreading enabled, this is ignored.
   * If this is 0, defaults to 1.
   */
  size_t thread_pool_size = 1;

  /**
   * If this is false, the `nodes` and `root_nodes` members of the returned `ClusterHierarchy` will be empty.
   */
  bool store_hierarchy = true;
};

/**
 * A cluster's / cluster group's bounding sphere and simplification error.
 */
struct ErrorBounds {
  /**
   * The bounding sphere's center.
   */
  float center[3]{};

  /**
   * The bounding sphere's radius.
   */
  float radius{};

  /**
   * The cluster's absolute simplification error.
   */
  float error = 0.0;
};

/**
 * A cluster's normal cone for front- / back-face culling.
 */
struct NormalCone {
  /**
   * The normal cone's apex.
   */
  float apex[3]{};

  /**
   * The normal cone's center axis.
   */
  float axis[3]{};

  /**
   * The cutoff angle for a cluster's normal cone.
   * If this is >= 1, the cluster cone does not contain any useful information and the cluster should be treated as if containing both back and front facing triangles.
   */
  float cutoff = 1.0;
};

/**
 * A cluster's / DAG node's error bounds.
 *
 * A cluster should be selected for a view, if its projected error is below a threshold but its parent group's error is above the same threshold.
 * A cluster is invisible if it's own bounds are not in the view frustum or does not contain any front- / back-facing triangles with respect to its normal cone.
 */
struct NodeErrorBounds {
  /**
   * The parent group's conservative error bounds.
   */
  ErrorBounds parent_error{};

  /**
   * The cluster's error bounds.
   */
  ErrorBounds cluster_error{};

  /**
   * The cluster's normal cone.
   */
  NormalCone normal_cone{};
};

/**
 * A node in the cluster hierarchy (DAG).
 * Each node represents a cluster and stores its child node indices.
 * Leaf nodes are identified by having no child node indices.
 */
struct Node {
  /**
   * The index of the node's corresponding cluster.
   */
  size_t cluster_index = 0;

  /**
   * The indices of the node's children.
   */
  std::vector<size_t> child_node_indices{};
};

/**
 * Cluster metadata.
 *
 * Shadows meshoptimizer's meshopt_Meshlet
 */
struct Cluster {
  /**
   * The cluster's offset in the array of vertex indices.
   */
  unsigned int vertex_offset = 0;

  /**
   * The cluster's offset in the array of triangles.
   */
  unsigned int triangle_offset = 0;

  /**
   * The number of vertex indices used by the cluster.
   */
  unsigned int vertex_count = 0;

  /**
   * The number of triangles in the cluster.
   */
  unsigned int triangle_count = 0;
};

/**
 * The cluster hierarchy created by `build_cluster_hierarchy`.
 *
 * The hierarchy is a Directed Acyclic Graph (DAG), where each node represents a cluster.
 * The leaf nodes represent the original high resolution triangle clusters built from the input mesh.
 * At each DAG level, n clusters are grouped, simplified, and split into m < n new clusters.
 * The m new clusters are then added to the DAG as parent nodes of the n clusters in the group.
 *
 * Nodes, clusters, and cluster errors in the hierarchy are all ordered such that nodes[i] and node_errors[i] are the node and node error belonging to the cluster clusters[i].
 */
struct ClusterHierarchy {
  /**
   * The topology of the DAG.
   */
  std::vector<Node> nodes{};

  /**
   * Indices of the root nodes in `nodes`.
   *
   * Ideally this contains only one element.
   * However, if the maximum hierarchy depth was reached before all clusters could be reduced to a single cluster, there will be multiple root nodes.
   */
  std::vector<size_t> root_nodes{};

  /**
   * Error bounds of clusters.
   */
  std::vector<NodeErrorBounds> node_errors{};

  /**
   * Clusters in the hierarchy.
   */
  std::vector<Cluster> clusters{};

  /**
   * Vertex indices of the clusters in the hierarchy.
   *
   * The first and last (exclusive) of a cluster c's vertices are:
   *    vertices[c.vertex_offset], vertices[c.vertex_offset + c.vertex_count]
   */
  std::vector<uint32_t> vertices{};

  /**
   * Triangles (triplets of indices into `vertices`) of the clusters in the hierarchy.
   *
   * The first and last (exclusive) of a cluster c's triangles are:
   *    vertices[c.triangle_offset], vertices[c.triangle_offset + c.triangle_count * 3]
   */
  std::vector<uint8_t> triangles{};
};

/**
 * Builds a cluster hierarchy for a given triangle mesh.
 *
 * Note that faceted meshes are currently not supported.
 * It is currently the user's responsibility to ensure the input mesh is contiguous, e.g., by first welding similar vertices.
 *
 * @param indices the input meshes vertex indices
 * @param vertices the input meshes vertices - the first 3 floats of a vertex are expected to store the position.
 * @param vertex_stride the size of each vertex in the vertices array
 * @param params tuning parameters for building the cluster hierarchy
 * @return Returns the triangle cluster hierarchy built for the input mesh.
 */
[[nodiscard]] ClusterHierarchy build_cluster_hierarchy(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride, const TriChiParams& params = {});
}

#endif  //TRICHI_HPP
