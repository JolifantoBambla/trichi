//
// Created by lukas on 28.08.24.
//

#include "meshoptimizer.h"

#include "impl.hpp"

namespace pmn {
[[nodiscard]] std::unordered_map<uint64_t, int> extract_cluster_edges(const Cluster& cluster) {
  const meshopt_Meshlet& meshlet = cluster.getMeshlet();
  std::unordered_map<uint64_t, int> edges{};
  for (size_t i = 0; i < meshlet.triangle_count; ++i) {
    const size_t triangle_offset = meshlet.triangle_offset + i * 3;
    const uint32_t a =
        cluster.buffers->vertices[meshlet.vertex_offset + cluster.buffers->triangles[triangle_offset + 0]];
    const uint32_t b =
        cluster.buffers->vertices[meshlet.vertex_offset + cluster.buffers->triangles[triangle_offset + 1]];
    const uint32_t c =
        cluster.buffers->vertices[meshlet.vertex_offset + cluster.buffers->triangles[triangle_offset + 2]];
    if (auto [edge, inserted] = edges.try_emplace(pack_sorted(a, b), 1); !inserted) {
      ++(edge->second);
    }
    if (auto [edge, inserted] = edges.try_emplace(pack_sorted(a, c), 1); !inserted) {
      ++(edge->second);
    }
    if (auto [edge, inserted] = edges.try_emplace(pack_sorted(b, c), 1); !inserted) {
      ++(edge->second);
    }
  }
  return std::move(edges);
}

void extract_boundary(const Cluster& cluster, std::vector<uint64_t>& boundary) {
  // find edges
  const auto edges = extract_cluster_edges(cluster);

  // find boundary = find edges that only appear once
  for (const auto& [edge_id, num_occurrences] : edges) {
    if (num_occurrences == 1) {
      boundary.push_back(edge_id);
    }
  }

  // sort boundary for later use of set_union
  std::sort(boundary.begin(), boundary.end());
}
}  // namespace pmn