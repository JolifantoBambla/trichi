/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>  // todo: remove
#include <numeric>
#include <valarray>

#include "meshoptimizer.h"
#include "metis.h"

#include "impl.hpp"
#include "trichi.hpp"

namespace trichi {
[[nodiscard]] Buffers buildClusters(
    const std::vector<uint32_t>& indices,
    const std::vector<float>& vertices,
    const size_t vertexCount,
    const size_t vertexStride,
    const size_t maxVertices,
    const size_t maxTriangles,
    const float coneWeight) {
  const size_t maxClusters = meshopt_buildMeshletsBound(indices.size(), maxVertices, maxTriangles);
  Buffers buffers = {
      .clusters = {},
      .vertices = std::vector<unsigned int>(maxClusters * maxVertices),
      .triangles = std::vector<unsigned char>(maxClusters * maxTriangles * 3),
  };

  std::vector<meshopt_Meshlet> clusters{maxClusters};

  // building the meshlets for lod 0 is the most time-consuming operation, guess there is not much more to do when it comes to performance optimization
  clusters.resize(meshopt_buildMeshlets(
      clusters.data(),
      buffers.vertices.data(),
      buffers.triangles.data(),
      indices.data(),
      indices.size(),
      vertices.data(),
      vertexCount,
      vertexStride,
      maxVertices,
      maxTriangles,
      coneWeight));

  // perf cost of this transform is insignificant
  std::transform(std::make_move_iterator(clusters.cbegin()), std::make_move_iterator(clusters.cend()), std::back_inserter(buffers.clusters), [](const auto& meshlet) {
    return Cluster {
      .vertexOffset = meshlet.vertex_offset,
      .triangleOffset = meshlet.triangle_offset,
      .vertexCount = meshlet.vertex_count,
      .triangleCount = meshlet.triangle_count,
    };
  });

  const auto& last = buffers.clusters.back();
  buffers.vertices.resize(last.vertexOffset + last.vertexCount);
  buffers.triangles.resize(last.triangleOffset + ((last.triangleCount * 3 + 3) & ~3));

  return std::move(buffers);
}

[[nodiscard]] Buffers buildParentCeshlets(
    const std::vector<uint32_t>& indices,
    const std::vector<float>& vertices,
    const size_t vertexCount,
    const size_t vertexStride,
    const size_t maxVertices,
    const size_t maxTriangles,
    const float coneWeight,
    const size_t maxClusters) {
  auto buffers = buildClusters(indices, vertices, vertexCount, vertexStride, maxVertices, maxTriangles, coneWeight);

  if (buffers.clusters.size() <= maxClusters) {
    for (size_t i = 0; i < buffers.clusters.size(); ++i) {
      const auto& meshlet = buffers.clusters[i];
      meshopt_optimizeMeshlet(
          &buffers.vertices[meshlet.vertexOffset],
          &buffers.triangles[meshlet.triangleOffset],
          meshlet.triangleCount,
          meshlet.vertexCount);
    }
  }

  return std::move(buffers);
}

[[nodiscard]] std::vector<std::vector<size_t>> buildFinalClusterGroup(const size_t size) {
  std::vector<std::vector<size_t>> groups{};
  auto& group = groups.emplace_back(size);
  std::iota(group.begin(), group.end(), 0);
  return std::move(groups);
}

[[nodiscard]] std::vector<unsigned int>
mergeGroup(const std::vector<ClusterIndex>& clusterIndices, const Buffers& buffers, const std::vector<size_t>& group, const size_t maxTriangles) {
  std::vector<uint32_t> groupIndices{};
  groupIndices.reserve(3 * maxTriangles * group.size());
  for (const auto& groupClusterIndex : group) {
    const auto& clusterIndex = clusterIndices[groupClusterIndex];
    const auto& cluster = buffers.clusters[clusterIndex.index];
    if (cluster.triangleOffset + (cluster.triangleCount * 3) > buffers.triangles.size()) {
      throw std::runtime_error("fuck");
    }
    std::transform(
        buffers.triangles.cbegin() + cluster.triangleOffset,
        buffers.triangles.cbegin() + cluster.triangleOffset + (cluster.triangleCount * 3),
        std::back_inserter(groupIndices),
        [&buffers, &cluster](const auto& vertex_index) {
          return buffers.vertices[cluster.vertexOffset + vertex_index];
        });
  }
  return std::move(groupIndices);
}

[[nodiscard]] std::pair<std::vector<unsigned int>, float> simplifyGroup(
    const std::vector<unsigned int>& groupIndices,
    const std::vector<float>& vertices,
    const size_t vertexCount,
    const size_t vertexStride,
    const size_t targetIndexCount,
    const float targetError) {
  // todo: maybe optional with attributes?
  std::vector<uint32_t> simplifiedIndices(groupIndices.size());
  uint32_t simplificationOptions = meshopt_SimplifyLockBorder | meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute;
  float simplificationError = 0.0f;
  simplifiedIndices.resize(meshopt_simplify(
      simplifiedIndices.data(),
      groupIndices.data(),
      groupIndices.size(),
      vertices.data(),
      vertexCount,
      vertexStride,
      targetIndexCount,
      targetError,
      simplificationOptions,
      &simplificationError));

  return std::make_pair(std::move(simplifiedIndices), simplificationError);
}

ClusterHierarchy buildClusterHierarchy(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, const size_t vertexStride, const Params& params) {
  // todo: remove
  const auto startTime = std::chrono::high_resolution_clock::now();

  if ((vertices.size() * sizeof(float)) % vertexStride != 0) {
    throw std::runtime_error("invalid vertex stride");
  }
  const size_t vertexCount = (vertices.size() * sizeof(float)) / vertexStride;

  const size_t maxVertices = params.maxVerticesPerCluster;
  const size_t maxTriangles = params.maxTrianglesPerCluster;
  const float coneWeight = params.clusterConeWeight;
  const size_t maxNumClustersPerGroup = params.targetClustersPerGroup;
  const size_t simplifyTargetIndexCount = std::min(maxVertices, maxTriangles) * 3 * 2;
  const size_t maxLodCount = params.maxHierarchyDepth;

  LoopRunner loopRunner{std::max(params.threadPoolSize, static_cast<size_t>(1))};

  std::vector<size_t> lodOffsets = {0};

  Buffers buffers = buildClusters(indices, vertices, vertexCount, vertexStride, maxVertices, maxTriangles, coneWeight);
  std::vector<NodeErrorBounds> nodeErrorBounds(buffers.clusters.size());
  std::vector<ClusterBounds> nodeClusterBounds(buffers.clusters.size());
  std::vector<Node> nodes(buffers.clusters.size());

  std::vector<ClusterIndex> clusterPool(buffers.clusters.size());

  loopRunner.loop(0, clusterPool.size(), [&](const size_t i) {
    const auto& cluster = buffers.clusters[i];
    meshopt_optimizeMeshlet(
        &buffers.vertices[cluster.vertexOffset],
        &buffers.triangles[cluster.triangleOffset],
        cluster.triangleCount,
        cluster.vertexCount);

    const auto clusterBounds = meshopt_computeMeshletBounds(
        &buffers.vertices[cluster.vertexOffset],
        &buffers.triangles[cluster.triangleOffset],
        cluster.triangleCount,
        vertices.data(),
        vertexCount,
        vertexStride);

    nodeErrorBounds[i].parentError.center[0] = clusterBounds.center[0];
    nodeErrorBounds[i].parentError.center[1] = clusterBounds.center[1];
    nodeErrorBounds[i].parentError.center[2] = clusterBounds.center[2];
    nodeErrorBounds[i].parentError.radius = clusterBounds.radius;
    nodeErrorBounds[i].parentError.error = std::numeric_limits<float>::max();
    nodeErrorBounds[i].clusterError.center[0] = clusterBounds.center[0];
    nodeErrorBounds[i].clusterError.center[1] = clusterBounds.center[1];
    nodeErrorBounds[i].clusterError.center[2] = clusterBounds.center[2];
    nodeErrorBounds[i].clusterError.radius = clusterBounds.radius;
    nodeErrorBounds[i].clusterError.error = 0.0;

    nodeClusterBounds[i].center[0] = clusterBounds.center[0];
    nodeClusterBounds[i].center[1] = clusterBounds.center[1];
    nodeClusterBounds[i].center[2] = clusterBounds.center[2];
    nodeClusterBounds[i].radius = clusterBounds.radius;
    nodeClusterBounds[i].normalCone.apex[0] = clusterBounds.cone_apex[0];
    nodeClusterBounds[i].normalCone.apex[1] = clusterBounds.cone_apex[1];
    nodeClusterBounds[i].normalCone.apex[2] = clusterBounds.cone_apex[2];
    nodeClusterBounds[i].normalCone.axis[0] = clusterBounds.cone_axis[0];
    nodeClusterBounds[i].normalCone.axis[1] = clusterBounds.cone_axis[1];
    nodeClusterBounds[i].normalCone.axis[2] = clusterBounds.cone_axis[2];

    clusterPool[i] = ClusterIndex{
        .index = i,
        .lod = 0,
    };

    nodes[i] = Node{
        .clusterIndex = i,
    };
  });

  for (size_t level = 1; level < maxLodCount; ++level) {
    // todo: remove
    const auto lodStartTime = std::chrono::high_resolution_clock::now();

    if (clusterPool.size() <= 1) {
      break;
    }
    lodOffsets.emplace_back(buffers.clusters.size());

    bool isLast = clusterPool.size() <= maxNumClustersPerGroup;

    const auto groups = isLast ? buildFinalClusterGroup(clusterPool.size())
                               : groupClusters(clusterPool, buffers, maxNumClustersPerGroup, loopRunner);

    constexpr float simplifyTargetError = std::numeric_limits<float>::max();

    std::atomic_size_t numNewMeshlets = 0;
    std::atomic_size_t numNewVertices = 0;
    std::atomic_size_t numNewTriangles = 0;
    std::atomic_size_t numNextClusters = 0;
    std::atomic_size_t numNotSimplified = 0;

    std::vector<Buffers> lodClusters(groups.size());
    std::vector<std::vector<ClusterIndex>> lodClusterIndices(groups.size());
    std::vector<std::vector<NodeErrorBounds>> lodErrorBounds(groups.size());
    std::vector<std::vector<ClusterBounds>> lodClusterBounds(groups.size());
    std::vector<std::vector<Node>> lodNodes(groups.size());

    // todo: cleanup
    loopRunner.loop(0, groups.size(), [&](const size_t i) {
      const auto& group = groups[i];
      if (group.empty()) {
        return;
      }

      bool simplified = group.size() != 1;
      if (simplified) {
        const auto groupIndices = mergeGroup(clusterPool, buffers, group, maxTriangles);
        const auto correctedIndexCount = std::min(simplifyTargetIndexCount, groupIndices.size());

        const size_t targetIndexCount =
            group.size() <= 2 ? correctedIndexCount / 2 : correctedIndexCount;
        const auto [simplifiedIndices, simplificationError] = simplifyGroup(
            groupIndices, vertices, vertexCount, vertexStride, targetIndexCount, simplifyTargetError);

        simplified = simplifiedIndices.size() < groupIndices.size();
        if (simplified) {
          auto groupClusters = std::move(buildParentCeshlets(
              simplifiedIndices,
              vertices,
              vertexCount,
              vertexStride,
              maxVertices,
              maxTriangles,
              coneWeight,
              group.size() - 1));

          simplified = groupClusters.clusters.size() < group.size();

          if (simplified) {
            numNewMeshlets += groupClusters.clusters.size();
            numNewVertices += groupClusters.vertices.size();
            numNewTriangles += groupClusters.triangles.size();
            numNextClusters += groupClusters.clusters.size();

            // merge error bounds to conservatively bound all child groups
            // the error bounds don't have to be a tight sphere around the group but must ensure monotonicity of the change in error from the root to its leaves
            // see Federico Ponchio, "Multiresolution structures for interactive visualization of very large 3D datasets", Section 4.2.3
            //
            // we use the method from meshoptimizer's nanite demo
            // https://github.com/zeux/meshoptimizer/blob/bfbbaddf38d6fc2311ba66762c6f7656a7c8dd79/demo/nanite.cpp#L67
            float groupBoundsCenter[3] = {0.0, 0.0, 0.0};
            float groupBoundsCenterWeight = 0.0;
            for (const size_t groupClusterIndex : group) {
              const auto& childError = nodeErrorBounds[clusterPool[groupClusterIndex].index].clusterError;
              groupBoundsCenter[0] += childError.center[0] * childError.radius;
              groupBoundsCenter[1] += childError.center[1] * childError.radius;
              groupBoundsCenter[2] += childError.center[2] * childError.radius;
              groupBoundsCenterWeight += childError.radius;
            }
            ErrorBounds groupErrorBounds{};
            groupErrorBounds.center[0] = groupBoundsCenter[0] / groupBoundsCenterWeight;
            groupErrorBounds.center[1] = groupBoundsCenter[1] / groupBoundsCenterWeight;
            groupErrorBounds.center[2] = groupBoundsCenter[2] / groupBoundsCenterWeight;
            groupErrorBounds.radius = 0.0;
            groupErrorBounds.error = simplificationError;
            for (const size_t groupClusterIndex : group) {
              const auto& childError = nodeErrorBounds[clusterPool[groupClusterIndex].index].clusterError;
              float dist[3] = {
                  groupErrorBounds.center[0] - childError.center[0],
                  groupErrorBounds.center[1] - childError.center[1],
                  groupErrorBounds.center[2] - childError.center[2],
              };
              groupErrorBounds.radius = std::max(groupErrorBounds.radius, childError.radius + std::sqrt(dist[0] * dist[0] + dist[1] * dist[1] + dist[2] * dist[2]));
              groupErrorBounds.error = std::max(groupErrorBounds.error, childError.error);
            }

            std::vector<size_t> childNodeIndices{};
            childNodeIndices.reserve(group.size());
            for (const size_t groupClusterIndex : group) {
              const size_t clusterIndex = clusterPool[groupClusterIndex].index;
              nodeErrorBounds[clusterIndex].parentError = groupErrorBounds;
              childNodeIndices.emplace_back(clusterIndex);
            }

            for (size_t parentIndex = 0; parentIndex < groupClusters.clusters.size(); ++parentIndex) {
              const auto& cluster = groupClusters.clusters[parentIndex];
              const auto clusterBounds = meshopt_computeMeshletBounds(
                  &groupClusters.vertices[cluster.vertexOffset],
                  &groupClusters.triangles[cluster.triangleOffset],
                  cluster.triangleCount,
                  vertices.data(),
                  vertexCount,
                  vertexStride);

              auto& nodeError = lodErrorBounds[i].emplace_back();
              nodeError.parentError.center[0] = groupErrorBounds.center[0];
              nodeError.parentError.center[1] = groupErrorBounds.center[1];
              nodeError.parentError.center[2] = groupErrorBounds.center[2];
              nodeError.parentError.radius = std::numeric_limits<float>::max();
              nodeError.parentError.error = std::numeric_limits<float>::max();
              nodeError.clusterError = groupErrorBounds;

              auto& nodeBounds = lodClusterBounds[i].emplace_back();
              nodeBounds.center[0] = clusterBounds.center[0];
              nodeBounds.center[1] = clusterBounds.center[1];
              nodeBounds.center[2] = clusterBounds.center[2];
              nodeBounds.radius = clusterBounds.radius;
              nodeBounds.normalCone.apex[0] = clusterBounds.cone_apex[0];
              nodeBounds.normalCone.apex[1] = clusterBounds.cone_apex[1];
              nodeBounds.normalCone.apex[2] = clusterBounds.cone_apex[2];
              nodeBounds.normalCone.axis[0] = clusterBounds.cone_axis[0];
              nodeBounds.normalCone.axis[1] = clusterBounds.cone_axis[1];
              nodeBounds.normalCone.axis[2] = clusterBounds.cone_axis[2];
              nodeBounds.normalCone.cutoff = clusterBounds.cone_cutoff;

              lodClusterIndices[i].emplace_back(ClusterIndex{
                  .index = parentIndex,
                  .lod = level,
              });

              lodNodes[i].emplace_back(Node{
                  .clusterIndex = parentIndex,
                  .childNodeIndices = childNodeIndices,
              });
            }

            lodClusters[i] = std::move(groupClusters);
          }
        }
      }
      if (!simplified) {
        numNotSimplified += group.size();
        numNextClusters += group.size();
        std::transform(
            group.cbegin(), group.cend(), std::back_inserter(lodClusterIndices[i]), [&clusterPool](const size_t clusterIndex) {
              return clusterPool[clusterIndex];
            });
      }
    });

    std::vector<ClusterIndex> nextClusters{};
    // merge clusters & prepare next iteration's cluster_pool
    {
      nextClusters.reserve(numNextClusters);

      buffers.clusters.reserve(buffers.clusters.size() + numNewMeshlets);
      buffers.vertices.reserve(buffers.vertices.size() + numNewVertices);
      buffers.triangles.reserve(buffers.triangles.size() + numNewTriangles);

      nodes.reserve(nodes.size() + numNewMeshlets);

      for (size_t i = 0; i < groups.size(); ++i) {
        if (!lodClusters[i].clusters.empty()) {
          for (size_t clusterIndex = 0; clusterIndex < lodClusterIndices[i].size(); ++clusterIndex) {
            auto& cluster = lodClusterIndices[i][clusterIndex];
            if (cluster.lod != level) {
              continue;
            }
            cluster.index += buffers.clusters.size();

            lodClusters[i].clusters[clusterIndex].vertexOffset += buffers.vertices.size();
            lodClusters[i].clusters[clusterIndex].triangleOffset += buffers.triangles.size();

            lodNodes[i][clusterIndex].clusterIndex += buffers.clusters.size();
          }
          buffers.clusters.insert(
              buffers.clusters.cend(),
              std::make_move_iterator(lodClusters[i].clusters.cbegin()),
              std::make_move_iterator(lodClusters[i].clusters.cend()));
          buffers.vertices.insert(
              buffers.vertices.cend(),
              std::make_move_iterator(lodClusters[i].vertices.cbegin()),
              std::make_move_iterator(lodClusters[i].vertices.cend()));
          buffers.triangles.insert(
              buffers.triangles.cend(),
              std::make_move_iterator(lodClusters[i].triangles.cbegin()),
              std::make_move_iterator(lodClusters[i].triangles.cend()));
          nodeErrorBounds.insert(
              nodeErrorBounds.cend(),
              std::make_move_iterator(lodErrorBounds[i].cbegin()),
              std::make_move_iterator(lodErrorBounds[i].cend()));
          nodeClusterBounds.insert(
              nodeClusterBounds.cend(),
              std::make_move_iterator(lodClusterBounds[i].cbegin()),
              std::make_move_iterator(lodClusterBounds[i].cend()));
          nodes.insert(
              nodes.cend(),
              std::make_move_iterator(lodNodes[i].cbegin()),
              std::make_move_iterator(lodNodes[i].cend()));
        }
        nextClusters.insert(
            nextClusters.cend(),
            std::make_move_iterator(lodClusterIndices[i].cbegin()),
            std::make_move_iterator(lodClusterIndices[i].cend()));
      }

      // todo: remove
      printf(
          "num clusters: lod %i: %i vs lod %i: %i; target: %i; not simplified: %i\n",
          int(level),
          int(numNewMeshlets),
          int(level - 1),
          int(clusterPool.size()),
          int(clusterPool.size()) / 2,
          int(numNotSimplified));
    }

    // todo: remove
    const auto lodEndTime = std::chrono::high_resolution_clock::now();
    printf(
        "lod %i: took %ld ms\n",
        int(level),
        std::chrono::duration_cast<std::chrono::milliseconds>(lodEndTime - lodStartTime).count());

    if (numNewMeshlets == 0) {
      break;
    }

    clusterPool = std::move(nextClusters);
  }

  size_t numRootNodes = buffers.clusters.size() - lodOffsets.back();
  std::vector<size_t> rootNodes{};
  for (size_t i = numRootNodes; i < buffers.clusters.size(); ++i) {
    rootNodes.emplace_back(i);
  }

  // todo: remove
  const auto endTime = std::chrono::high_resolution_clock::now();
  printf(
      "create dag: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

  return ClusterHierarchy{
      .nodes = std::move(nodes),
      .rootNodes = std::move(rootNodes),
      .errors = std::move(nodeErrorBounds),
      .bounds = std::move(nodeClusterBounds),
      .clusters = std::move(buffers.clusters),
      .vertices = std::move(buffers.vertices),
      .triangles = std::move(buffers.triangles),
  };
}

}  // namespace trichi
