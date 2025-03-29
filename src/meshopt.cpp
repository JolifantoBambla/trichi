/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

#include <unordered_set>

#include "meshoptimizer.h"

#include "impl.hpp"

namespace trichi {
[[nodiscard]] std::vector<std::vector<size_t>> groupClusters(
    const std::vector<ClusterIndex>& clusterIndices,
    const Buffers& buffers,
    const size_t maxClustersPerGroup,
    LoopRunner& loopRunner) {

  std::vector<uint32_t> clusterToGroup(clusterIndices.size());
  std::vector<uint32_t> indexCountPerCluster(clusterIndices.size());
  std::vector<uint32_t> indexOffsetPerCluster(clusterIndices.size());
  size_t totalIndexCount = 0;

  for (size_t i = 0; i < clusterIndices.size(); ++i) {
    indexCountPerCluster[i] = buffers.clusters[clusterIndices[i].index].triangleCount * 3;
    indexOffsetPerCluster[i] = totalIndexCount;
    totalIndexCount += indexCountPerCluster[i];
  }

  std::vector<uint32_t> indicesPerCluster(totalIndexCount);
  std::unordered_set<uint32_t> uniqueVertices{};
  for (size_t i = 0; i < clusterIndices.size(); ++i) {
    const auto& cluster = buffers.clusters[clusterIndices[i].index];
    for (size_t t = 0; t < cluster.triangleCount; ++t) {
      const size_t indexOffset = t * 3;
      const size_t triangleOffset = cluster.triangleOffset + indexOffset;
      for (size_t v = 0; v < 3; ++v) {
        const uint32_t vertexIndex = buffers.vertices[cluster.vertexOffset + buffers.triangles[triangleOffset + v]];
        indicesPerCluster[indexOffsetPerCluster[i] + indexOffset + v] = vertexIndex;
        uniqueVertices.insert(vertexIndex);
      }
    }
  };

  return resolveGroups(
      clusterToGroup,
      meshopt_partitionClusters(
          clusterToGroup.data(),
          indicesPerCluster.data(),
          indicesPerCluster.size(),
          indexCountPerCluster.data(),
          clusterIndices.size(),
          uniqueVertices.size(),
          maxClustersPerGroup)
      );
}
}  // namespace trichi