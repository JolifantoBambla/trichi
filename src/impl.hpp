/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

#ifndef TRICHI_IMPL_HPP
#define TRICHI_IMPL_HPP

#include <unordered_map>
#include <vector>

#include "meshoptimizer.h"

#include "trichi.hpp"
#include "util.hpp"

namespace trichi {
struct Buffers {
  std::vector<Cluster> clusters;
  std::vector<unsigned int> vertices;
  std::vector<unsigned char> triangles;
};

struct ClusterIndex {
  size_t index{};
  size_t lod{};
};

[[nodiscard]] std::unordered_map<uint64_t, int> extractClusterEdges(const ClusterIndex& clusterIndex, const Buffers& buffers);

void extractBoundary(const ClusterIndex& clusterIndex, const Buffers& buffers, std::vector<uint64_t>& boundary);

[[nodiscard]] std::vector<std::vector<uint64_t>> extractBoundaries(const std::vector<ClusterIndex>& clusterIndices, const Buffers& buffers, LoopRunner& loopRunner);

template <typename Index>
[[nodiscard]] std::vector<std::vector<size_t>> resolveGroups(const std::vector<Index>& partition, const size_t numGroups) {
  auto groups = std::vector<std::vector<size_t>>(numGroups);
  for (size_t i = 0; i < partition.size(); ++i) {
    groups[partition[i]].push_back(i);
  }
  return std::move(groups);
}

[[nodiscard]] std::vector<std::vector<size_t>> groupClusters(
    const std::vector<ClusterIndex>& clusterIndices,
    const Buffers& buffers,
    const size_t maxClustersPerGroup,
    LoopRunner& loopRunner);
}  // namespace trichi

#endif  //TRICHI_IMPL_HPP
