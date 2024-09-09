/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

#include "meshoptimizer.h"

#include "impl.hpp"

namespace trichi {
[[nodiscard]] std::unordered_map<uint64_t, int> extract_cluster_edges(const ClusterIndex& cluster, const MeshletsBuffers& lods) {
  const auto& meshlet = lods.clusters[cluster.index];
  std::unordered_map<uint64_t, int> edges{};
  for (size_t i = 0; i < meshlet.triangle_count; ++i) {
    const size_t triangle_offset = meshlet.triangle_offset + i * 3;
    const uint32_t a =
        lods.vertices[meshlet.vertex_offset + lods.triangles[triangle_offset + 0]];
    const uint32_t b =
        lods.vertices[meshlet.vertex_offset + lods.triangles[triangle_offset + 1]];
    const uint32_t c =
        lods.vertices[meshlet.vertex_offset + lods.triangles[triangle_offset + 2]];
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

void extract_boundary(const ClusterIndex& cluster, const MeshletsBuffers& lods, std::vector<uint64_t>& boundary) {
  // find edges
  const auto edges = extract_cluster_edges(cluster, lods);

  // find boundary = find edges that only appear once
  for (const auto& [edge_id, num_occurrences] : edges) {
    if (num_occurrences == 1) {
      boundary.push_back(edge_id);
    }
  }

  // sort boundary for later use of set_union
  std::sort(boundary.begin(), boundary.end());
}

[[nodiscard]] std::vector<std::vector<uint64_t>> extract_boundaries(const std::vector<ClusterIndex>& clusters, const MeshletsBuffers& lods, LoopRunner& loop_runner) {
  std::vector<std::vector<uint64_t>> boundaries(clusters.size());
  loop_runner.loop(0, clusters.size(), [&clusters, &lods, &boundaries](const size_t i) { extract_boundary(clusters[i], lods, boundaries[i]); });
  return std::move(boundaries);
}

}  // namespace trichi