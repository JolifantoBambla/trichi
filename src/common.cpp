/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

#include "meshoptimizer.h"

#include "impl.hpp"

namespace trichi {
[[nodiscard]] std::unordered_map<uint64_t, int> extractClusterEdges(const ClusterIndex& clusterIndex, const Buffers& buffers) {
  const auto& cluster = buffers.clusters[clusterIndex.index];
  std::unordered_map<uint64_t, int> edges{};
  for (size_t i = 0; i < cluster.triangleCount; ++i) {
    const size_t triangle_offset = cluster.triangleOffset + i * 3;
    const uint32_t a =
        buffers.vertices[cluster.vertexOffset + buffers.triangles[triangle_offset + 0]];
    const uint32_t b =
        buffers.vertices[cluster.vertexOffset + buffers.triangles[triangle_offset + 1]];
    const uint32_t c =
        buffers.vertices[cluster.vertexOffset + buffers.triangles[triangle_offset + 2]];
    if (auto [edge, inserted] = edges.try_emplace(packSorted(a, b), 1); !inserted) {
      ++(edge->second);
    }
    if (auto [edge, inserted] = edges.try_emplace(packSorted(a, c), 1); !inserted) {
      ++(edge->second);
    }
    if (auto [edge, inserted] = edges.try_emplace(packSorted(b, c), 1); !inserted) {
      ++(edge->second);
    }
  }
  return std::move(edges);
}

void extractBoundary(const ClusterIndex& clusterIndex, const Buffers& buffers, std::vector<uint64_t>& boundary) {
  // find edges
  const auto edges = extractClusterEdges(clusterIndex, buffers);

  // find boundary = find edges that only appear once
  for (const auto& [edgeId, numOccurrences] : edges) {
    if (numOccurrences == 1) {
      boundary.push_back(edgeId);
    }
  }

  // sort boundary for later use of set_union
  std::sort(boundary.begin(), boundary.end());
}

[[nodiscard]] std::vector<std::vector<uint64_t>> extractBoundaries(const std::vector<ClusterIndex>& clusterIndices, const Buffers& buffers, LoopRunner& loopRunner) {
  std::vector<std::vector<uint64_t>> boundaries(clusterIndices.size());
  loopRunner.loop(0, clusterIndices.size(), [&clusterIndices, &buffers, &boundaries](const size_t i) { extractBoundary(clusterIndices[i], buffers, boundaries[i]); });
  return std::move(boundaries);
}

}  // namespace trichi