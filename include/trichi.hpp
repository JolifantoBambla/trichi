//
// Created by lukas on 21.08.24.
//

#ifndef TRICHI_HPP
#define TRICHI_HPP

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

struct Bounds {
  float center[3];
  float radius;
  float error;
};

struct ClusterCone {
  float cone_apex[3];
  float cone_cutoff;
  float cone_axis[3];
};

struct ClusterHierarchy {

};

// todo: no defaults
void build_cluster_hierarchy(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride, const TrichiParams& params = {});
}

#endif  //TRICHI_HPP
