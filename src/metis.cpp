//
// Created by lukas on 28.08.24.
//

#include <array>
#include <atomic>
#include <chrono>

#include "metis.h"

#include "impl.hpp"

namespace pmn {
struct Graph {
  std::vector<idx_t> xadj;
  std::vector<idx_t> adjacency;
  std::vector<idx_t> adjwght;
  bool is_contiguous;
};

[[nodiscard]] Graph build_cluster_graph(const std::vector<Cluster>& clusters, LoopRunner& loop_runner) {
  const auto boundaries = extract_boundaries(clusters, loop_runner);

  // compute metis inputs here as well (xadj is exclusive scan of node degrees, adjacency is list of neighboring node indices, adjwght is list of edge weights)
  std::atomic<size_t> no_neighbors_count = 0;
  std::atomic<size_t> no_neighbors_count2 = 0;
  std::vector<std::unordered_map<size_t, size_t>> graph_edge_weights{};
  graph_edge_weights.reserve(clusters.size());
  std::vector<idx_t> xadj = {0};
  xadj.reserve(clusters.size() + 1);
  std::vector<idx_t> adjacency{};
  std::vector<idx_t> adjwght{};

  // todo: parallelize - either brute force set intersection n^2 but parallel or split into a parallel set intersection and a sequential resolving loop (or a parallel exclusive sum and a parallel resolving loop - could be overkill who knows)
  for (size_t i = 0; i < boundaries.size(); ++i) {
    const auto& boundary = boundaries[i];

    graph_edge_weights.emplace_back();
    auto& node_edge_weights = graph_edge_weights.back();

    xadj.push_back(xadj[i]);

    // loop over vertices that have already been processed (need to store both (u,v) & (v,u))
    for (size_t j = 0; j < i; ++j) {
      if (graph_edge_weights[j].contains(i)) {
        ++xadj[i + 1];
        adjacency.push_back(static_cast<idx_t>(j));
        adjwght.push_back(static_cast<idx_t>(graph_edge_weights[j][i]));
        node_edge_weights[j] = graph_edge_weights[j][i];
      }
    }

    // loop over vertices that have not been processed yet
    for (size_t j = i + 1; j < boundaries.size(); ++j) {
      const auto shared_boundary_length = intersection_size(boundary, boundaries[j]);
      if (shared_boundary_length > 0) {
        ++xadj[i + 1];
        adjacency.push_back(static_cast<idx_t>(j));
        adjwght.push_back(static_cast<idx_t>(shared_boundary_length));
        node_edge_weights[j] = shared_boundary_length;
      }
    }

    // todo: remove (debug) - or maybe handle this?
    if ((xadj[i + 1] - xadj[i]) == 0) {
      ++no_neighbors_count;
      // todo: if mesh is broken, boundary can't be determined by comparing indices -> either let user fix their mesh or add compatibility mode that tries to find neighboring meshlets by comparing their vertex positions
      printf("%i: no neighbors\n", int(i));
    }
    if (graph_edge_weights[i].empty()) {
      ++no_neighbors_count2;
    }
  }

  return Graph{
      .xadj = std::move(xadj),
      .adjacency = std::move(adjacency),
      .adjwght = std::move(adjwght),
      .is_contiguous = no_neighbors_count == 0,
  };
}

[[nodiscard]] std::vector<std::vector<size_t>> resolve_groups(const std::vector<idx_t>& partition, size_t num_groups) {
  auto groups = std::vector<std::vector<size_t>>(num_groups);
  for (size_t i = 0; i < partition.size(); ++i) {
    groups[partition[i]].push_back(i);
  }
  return std::move(groups);
}

[[nodiscard]] std::array<idx_t, METIS_NOPTIONS> createPartitionOptions(bool is_contiguous = true) {
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
  options[METIS_OPTION_CONTIG] = idx_t(is_contiguous);  // 1 -> force contiguous partitions
  //options[METIS_OPTION_SEED] = 0; // seed for rng
  options[METIS_OPTION_NUMBERING] = 0;  // 0 -> result is 0-indexed
#ifndef NDEBUG
  options[METIS_OPTION_DBGLVL] |= METIS_DBG_INFO;
#endif
  // todo: add ways to set debug level options for other stuff, e.g., timing
  return options;
}

// todo: mt-kahypar (https://github.com/kahypar/mt-kahypar) looks very promising for graph partitioning - no static lib though, so needs some work for wasm build
[[nodiscard]] std::vector<std::vector<size_t>> partition_graph(Graph graph, size_t max_clusters_per_group) {
  auto numVertices = static_cast<idx_t>(graph.xadj.size() - 1);
  idx_t numConstraints = 1;  // 1 is the minimum allowed value
  idx_t numParts = std::max(numVertices / static_cast<idx_t>(max_clusters_per_group), 2);
  idx_t edgeCut = 0;
  std::vector<idx_t> partition = std::vector<idx_t>(numVertices, 0);
  std::array<idx_t, METIS_NOPTIONS> options = createPartitionOptions(graph.is_contiguous);

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

  return resolve_groups(partition, numParts);
}

[[nodiscard]] std::vector<std::vector<size_t>> group_clusters(
    const std::vector<Cluster>& clusters,
    size_t max_clusters_per_group,
    LoopRunner& loop_runner) {
  return std::move(partition_graph(std::move(build_cluster_graph(clusters, loop_runner)), max_clusters_per_group));
}
}  // namespace pmn