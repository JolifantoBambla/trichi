/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

#include <array>
#include <atomic>
#include <chrono>

#include "metis.h"

#include "impl.hpp"

namespace trichi {
struct Graph {
  std::vector<idx_t> xadj;
  std::vector<idx_t> adjacency;
  std::vector<idx_t> adjwght;
  bool isContiguous;
};

[[nodiscard]] Graph buildClusterGraph(const std::vector<ClusterIndex>& clusterIndices, const Buffers& buffers, LoopRunner& loopRunner) {
  const auto boundaries = extractBoundaries(clusterIndices, buffers, loopRunner);

  std::atomic<size_t> adjacencySize = 0;

  std::vector<idx_t> valenceForward( clusterIndices.size(), 0);
  std::vector<std::vector<idx_t>> adjacencyForward{clusterIndices.size()};
  std::vector<std::vector<idx_t>> adjwghtForward{clusterIndices.size()};

  // forward pass on adjacency edge weights - this is expensive, so we do this in parallel
  // (doing a parallel forward and a sequential resolve pass is ~5s faster for 4m tris than doing a parallel bidirectional pass on my machine)
  loopRunner.loop(0, boundaries.size(), [&](const size_t i) {
    const auto& boundary = boundaries[i];
    // loop over vertices that have not been processed yet
    for (size_t j = i + 1; j < boundaries.size(); ++j) {
      if (const auto sharedBoundaryLength = intersectionSize(boundary, boundaries[j]); sharedBoundaryLength > 0) {
        ++valenceForward[i];
        adjacencyForward[i].push_back(static_cast<idx_t>(j));
        adjwghtForward[i].push_back(static_cast<idx_t>(sharedBoundaryLength));
      }
    }
    adjacencySize += adjacencyForward[i].size() * 2;
  });

  bool isContiguous = true;

  // stores the resolved adjacency_forward & edge weights
  std::vector<idx_t> xadj( clusterIndices.size() + 1, 0);
  std::vector<idx_t> adjacency{};
  std::vector<idx_t> adjwght{};
  adjacency.reserve(adjacencySize);
  adjwght.reserve(adjacencySize);

  // temp vectors to store adjacency_forward & edge weights from earlier nodes (we need each edge in both directions)
  std::vector<std::vector<idx_t>> adjacencyPrev{clusterIndices.size()};
  std::vector<std::vector<idx_t>> adjwghtPrev{clusterIndices.size()};

  // resolve adjacency_forward & edge weights
  for (size_t i = 0; i < boundaries.size(); ++i) {
    const idx_t valence = valenceForward[i] + static_cast<idx_t>(adjacencyPrev[i].size());
    if (valence == 0) {
      isContiguous = false;
    }
    xadj[i + 1] += valence + xadj[i];
    for (size_t j = 0; j < adjacencyForward[i].size(); ++j) {
      adjacencyPrev[adjacencyForward[i][j]].push_back(static_cast<idx_t>(i));
      adjwghtPrev[adjacencyForward[i][j]].push_back(adjwghtForward[i][j]);
    }
    adjacency.insert(adjacency.cend(), adjacencyPrev[i].cbegin(), adjacencyPrev[i].cend());
    adjwght.insert(adjwght.cend(), adjwghtPrev[i].cbegin(), adjwghtPrev[i].cend());
    adjacency.insert(adjacency.cend(), adjacencyForward[i].cbegin(), adjacencyForward[i].cend());
    adjwght.insert(adjwght.cend(), adjwghtForward[i].cbegin(), adjwghtForward[i].cend());
  }

  return Graph{
      .xadj = std::move(xadj),
      .adjacency = std::move(adjacency),
      .adjwght = std::move(adjwght),
      .isContiguous = isContiguous,
  };
}

[[nodiscard]] std::vector<std::vector<size_t>> resolveGroups(const std::vector<idx_t>& partition, const size_t numGroups) {
  auto groups = std::vector<std::vector<size_t>>(numGroups);
  for (size_t i = 0; i < partition.size(); ++i) {
    groups[partition[i]].push_back(i);
  }
  return std::move(groups);
}

[[nodiscard]] std::array<idx_t, METIS_NOPTIONS> createPartitionOptions(const bool isContiguous = true) {
  std::array<idx_t, METIS_NOPTIONS> options{};
  METIS_SetDefaultOptions(options.data());
  options[METIS_OPTION_OBJTYPE] =
      METIS_PTYPE_KWAY;  // partitioning method. no idea why this can be set if the method is in the function name? also, what's the difference between the two methods?
  options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
  // todo: check out possible options
  options[METIS_OPTION_CTYPE] =
      METIS_CTYPE_SHEM;  // matching scheme: RM: random matching, SHEM: sorted heavy edge matching (is that what I want or no?)
  options[METIS_OPTION_IPTYPE] = METIS_IPTYPE_GROW;  // grow: greedy; algorithm for initial partitioning
  //options[METIS_OPTION_RTYPE] = 0;  // algorithm for refinement
  options[METIS_OPTION_NO2HOP] =
      1;  // 1 -> coarsening will not perform 2-hop matchings - I think that 2-hops are irrelevant for meshlet grouping, right?
  options[METIS_OPTION_NCUTS] =
      1;  // number of partitionings that will be computed, the final one is the one with best result
  options[METIS_OPTION_NITER] = 10;  // number of refinement steps at each coarsening step
  //options[METIS_OPTION_UFACTOR] = 0; // default for rb = 1, kway = 30
  //options[METIS_OPTION_MINCONN] = 0; // 1 -> explicitly minimize connectivity between groups
  options[METIS_OPTION_CONTIG] = idx_t(isContiguous);  // 1 -> force contiguous partitions
  //options[METIS_OPTION_SEED] = 0; // seed for rng
  options[METIS_OPTION_NUMBERING] = 0;  // 0 -> result is 0-indexed
#ifndef NDEBUG
  options[METIS_OPTION_DBGLVL] |= METIS_DBG_INFO;
#endif
  // todo: add ways to set debug level options for other stuff, e.g., timing
  return options;
}

// todo: mt-kahypar (https://github.com/kahypar/mt-kahypar) looks very promising for graph partitioning - no static lib though, so needs some work for wasm build
[[nodiscard]] std::vector<std::vector<size_t>> partitionGraph(Graph graph, const size_t maxClustersPerGroup) {
  auto numVertices = static_cast<idx_t>(graph.xadj.size() - 1);
  idx_t numConstraints = 1;  // 1 is the minimum allowed value
  idx_t numParts = std::max(numVertices / static_cast<idx_t>(maxClustersPerGroup), 2);
  idx_t edgeCut = 0;
  std::vector<idx_t> partition = std::vector<idx_t>(numVertices, 0);
  std::array<idx_t, METIS_NOPTIONS> options = createPartitionOptions(graph.isContiguous);

  const auto partitionResult = METIS_PartGraphKway(
      &numVertices,            // number of vertices
      &numConstraints,         // number of constraints (=1 -> edge weights? or just 1 because that's the minimum?)
      graph.xadj.data(),       // adjacency information: exclusive scan of vertex degrees
      graph.adjacency.data(),  // adjacency information: list of edges per node
      nullptr,                 // vertex weights
      nullptr,  // number of vertices for computing the total communication volume -> not used, we use min edge cut
      graph.adjwght.data(),  // edge weights
      &numParts,  // number of groups -> we want to half the number of tris by grouping 4 clusters that are then simplified and then split in half
      nullptr,    // weight for each partition -> nullptr means we want an equally distributed partition
      nullptr,    // the allowed load imbalance tolerance -> we use the default
      options.data(),  // options
      &edgeCut,        // the edge cut (or total communication volume)
      partition
          .data());  // the partition vector of the graph (0- or 1-indexing depends on value of METIS_OPTION_NUMBERING)

  if (partitionResult != METIS_OK) {
    if (partitionResult == METIS_ERROR_INPUT) {
      throw std::runtime_error("could not partition graph - input error");
    } else if (partitionResult == METIS_ERROR_MEMORY) {
      throw std::runtime_error("could not partition graph - not enough memory");
    } else {
      throw std::runtime_error("could not partition graph - unknown error");
    }
  }

  return resolveGroups(partition, numParts);
}

[[nodiscard]] std::vector<std::vector<size_t>> groupClusters(
    const std::vector<ClusterIndex>& clusterIndices,
    const Buffers& buffers,
    const size_t maxClustersPerGroup,
    LoopRunner& loopRunner) {
  return std::move(partitionGraph(
      std::move(buildClusterGraph(clusterIndices, buffers, loopRunner)),
      maxClustersPerGroup));
}
}  // namespace trichi