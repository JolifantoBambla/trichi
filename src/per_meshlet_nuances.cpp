//
// Created by lukas on 24.08.24.
//
//
// Created by lukas on 21.08.24.
//

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>  // todo: remove
#include <optional>
#include <numeric>
#include <span>
#include <unordered_map>

#include "per_meshlet_nuances.hpp"

#include "meshoptimizer.h"
#include "metis.h"

// https://stackoverflow.com/questions/32640327/how-to-compute-the-size-of-an-intersection-of-two-stl-sets-in-c
struct Counter {
  struct value_type {
    template <typename T>
    value_type(const T&) {}
  };

  void push_back(const value_type&) { ++count; }

  size_t count = 0;
};

template <typename T1, typename T2>
size_t intersection_size(const T1& s1, const T2& s2) {
  Counter c{};
  std::set_intersection(s1.begin(), s1.end(), s2.begin(), s2.end(), std::back_inserter(c));
  return c.count;
}

template <typename T1, typename T2>
size_t union_size(const T1& s1, const T2& s2) {
  Counter c{};
  std::set_union(s1.begin(), s1.end(), s2.begin(), s2.end(), std::back_inserter(c));
  return c.count;
}

// todo: I have a lot of embarrassingly parallel steps in here - std::async adds a lot of overhead for creating and destroying threads every time, so thread pool would be good. maybe use https://github.com/hosseinmoein/Leopard ?

namespace pmn {
struct MeshletId {
  uint32_t index;
  uint32_t level;

  bool operator==(const MeshletId& other) const { return index == other.index && level == other.level; }
};
}  // namespace pmn

template <>
struct std::hash<pmn::MeshletId> {
  std::size_t operator()(const pmn::MeshletId& id) const {
    return std::hash<uint64_t>()(static_cast<uint64_t>(id.index) << 32) | id.level;
  }
};

namespace pmn {
struct Meshlets {
  std::vector<meshopt_Meshlet> meshlets;
  std::vector<unsigned int> vertices;
  std::vector<unsigned char> triangles;
  std::vector<meshopt_Bounds> bounds;
  std::vector<float> errors;
};

struct Cluster {
  size_t index;
  const Meshlets* buffers;

  [[nodiscard]] const meshopt_Meshlet& getMeshlet() const { return buffers->meshlets[index]; }
};

struct DagNode {
  size_t children_offset;
  size_t num_children;
};

struct Dag {
  std::unordered_map<MeshletId, DagNode> nodes;
  std::vector<MeshletId> meshlet_ids;
  std::unordered_map<uint32_t, Meshlets> meshlets;
};

[[nodiscard]] uint64_t pack_sorted(uint32_t a, uint32_t b) {
  return (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
}

[[nodiscard]] std::unordered_map<uint64_t, int> extract_cluster_edges(
    const std::vector<Cluster>& clusters,
    size_t meshlet_index) {
  const Cluster& cluster = clusters[meshlet_index];
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

[[nodiscard]] Meshlets build_meshlets(
    const std::vector<uint32_t>& indices,
    const std::vector<float>& vertices,
    size_t vertex_count,
    size_t vertex_stride,
    size_t max_vertices,
    size_t max_triangles,
    float cone_weight,
    std::optional<size_t> max_allowed_meshlets = std::nullopt) {
  size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);
  Meshlets meshlets = {
      .meshlets = std::vector<meshopt_Meshlet>(max_meshlets),
      .vertices = std::vector<unsigned int>(max_meshlets * max_vertices),
      .triangles = std::vector<unsigned char>(max_meshlets * max_triangles * 3),
  };

  const auto start_build_meshlets = std::chrono::high_resolution_clock::now();
  meshlets.meshlets.resize(meshopt_buildMeshlets(
      meshlets.meshlets.data(),
      meshlets.vertices.data(),
      meshlets.triangles.data(),
      indices.data(),
      indices.size(),
      vertices.data(),
      vertex_count,
      vertex_stride,
      max_vertices,
      max_triangles,
      cone_weight));
  const auto end_build_meshlets = std::chrono::high_resolution_clock::now();
  /*
  printf(
      "build meshlets: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_build_meshlets - start_build_meshlets).count());
    */

  const meshopt_Meshlet& last = meshlets.meshlets.back();
  meshlets.vertices.resize(last.vertex_offset + last.vertex_count);
  meshlets.triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));

  if (max_allowed_meshlets.has_value() && meshlets.meshlets.size() <= max_allowed_meshlets.value()) {
    const auto start_optimize_meshlets = std::chrono::high_resolution_clock::now();
    for (const auto& meshlet : meshlets.meshlets) {
      meshopt_optimizeMeshlet(
          &meshlets.vertices[meshlet.vertex_offset],
          &meshlets.triangles[meshlet.triangle_offset],
          meshlet.triangle_count,
          meshlet.vertex_count);
    }
    const auto end_optimize_meshlets = std::chrono::high_resolution_clock::now();
    /*
  printf(
      "optimize meshlets: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_optimize_meshlets - start_optimize_meshlets).count());
  */
  }

  return std::move(meshlets);
}

[[nodiscard]] std::vector<std::vector<uint64_t>> extract_boundaries(const std::vector<Cluster>& clusters) {
  // compute boundary for each cluster
  std::vector<std::vector<uint64_t>> boundaries(clusters.size());
  const auto start_meshlet_boundary = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < clusters.size(); ++i) {
    // find edges
    const auto edges = extract_cluster_edges(clusters, i);

    // find boundary = find edges that only appear once
    auto& boundary = boundaries[i];
    for (const auto& [edge_id, num_occurrences] : edges) {
      if (num_occurrences == 1) {
        boundary.push_back(edge_id);
      }
    }

    // sort boundary for later use of set_union
    std::sort(boundary.begin(), boundary.end());
  }
  const auto end_meshlet_boundary = std::chrono::high_resolution_clock::now();
  /*
  printf(
      "meshlet boundary: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_meshlet_boundary - start_meshlet_boundary).count());
  */

  return std::move(boundaries);
}

struct Graph {
  std::vector<idx_t> xadj;
  std::vector<idx_t> adjacency;
  std::vector<idx_t> adjwght;
  bool is_contiguous;
};

[[nodiscard]] Graph build_cluster_graph(const std::vector<Cluster>& clusters) {
  const auto boundaries = extract_boundaries(clusters);

  // compute metis inputs here as well (xadj is exclusive scan of node degrees, adjacency is list of neighboring node indices, adjwght is list of edge weights)
  std::atomic<size_t> no_neighbors_count = 0;
  std::atomic<size_t> no_neighbors_count2 = 0;
  std::vector<std::unordered_map<size_t, size_t>> graph_edge_weights{};
  graph_edge_weights.reserve(clusters.size());
  std::vector<idx_t> xadj = {0};
  xadj.reserve(clusters.size() + 1);
  std::vector<idx_t> adjacency{};
  std::vector<idx_t> adjwght{};

  const auto start_build_meshlet_graph = std::chrono::high_resolution_clock::now();
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
    if ((xadj[i + 1] - xadj[i]) == 0) {
      ++no_neighbors_count;
      // todo: if mesh is broken, boundary can't be determined by comparing indices -> either let user fix their mesh or add compatibility mode that tries to find neighboring meshlets by comparing their vertex positions
      printf("%i: no neighbors\n", int(i));
    }
    if (graph_edge_weights[i].empty()) {
      ++no_neighbors_count2;
    }
  }
  const auto end_build_meshlet_graph = std::chrono::high_resolution_clock::now();
  /*
  printf(
      "meshlet graph: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_build_meshlet_graph - start_build_meshlet_graph)
          .count());

  printf(
      "%i of %i meshlets have no neighbors, %i have no neighborhood\n",
      int(no_neighbors_count),
      int(clusters.size()),
      int(no_neighbors_count2));
  */

  return Graph{
      .xadj = std::move(xadj),
      .adjacency = std::move(adjacency),
      .adjwght = std::move(adjwght),
      .is_contiguous = no_neighbors_count == 0,
  };
}

[[nodiscard]] std::vector<std::vector<size_t>> resolve_groups(const std::vector<idx_t>& partition, size_t num_groups) {
  auto groups = std::vector<std::vector<size_t>>(num_groups);
  const auto start_parse_partition = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < partition.size(); ++i) {
    groups[partition[i]].push_back(i);
  }
  const auto end_parse_partition = std::chrono::high_resolution_clock::now();
  /*
  printf(
      "parse partition: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_parse_partition - start_parse_partition).count());
  */
  return std::move(groups);
}

// todo: mt-kahypar (https://github.com/kahypar/mt-kahypar) looks very promising for graph partitioning - no static lib though, so needs some work for wasm build
[[nodiscard]] std::vector<std::vector<size_t>> partition_graph(Graph graph, size_t max_clusters_per_group) {
  auto numVertices = static_cast<idx_t>(graph.xadj.size() - 1);
  idx_t numConstraints = 1;  // 1 is the minimum allowed value
  idx_t numParts = std::max(numVertices / static_cast<idx_t>(max_clusters_per_group), 2);
  idx_t edgeCut = 0;
  std::vector<idx_t> partition = std::vector<idx_t>(numVertices, 0);
  std::array<idx_t, METIS_NOPTIONS> options = createPartitionOptions(graph.is_contiguous);

  const auto start_partition_meshlet_graph = std::chrono::high_resolution_clock::now();
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
  const auto end_partition_meshlet_graph = std::chrono::high_resolution_clock::now();
  /*
  printf(
      "graph partition: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_partition_meshlet_graph - start_partition_meshlet_graph)
          .count());
  */

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
    size_t max_clusters_per_group = 4) {
  return std::move(partition_graph(std::move(build_cluster_graph(clusters)), max_clusters_per_group));
}

[[nodiscard]] std::vector<std::vector<size_t>> build_final_cluster_group(size_t size) {
  std::vector<std::vector<size_t>> groups{};
  auto& group = groups.emplace_back(size);
  std::iota(group.begin(), group.end(), 0);
  return std::move(groups);
}

[[nodiscard]] std::vector<unsigned int>
merge_group(const std::vector<Cluster>& clusters, const std::vector<size_t>& group, size_t max_triangles) {
  std::vector<uint32_t> group_indices{};
  group_indices.reserve(3 * max_triangles * group.size());
  for (const auto& meshlet_index : group) {
    const auto& cluster = clusters[meshlet_index];
    const auto& meshlet = cluster.getMeshlet();
    std::transform(
        cluster.buffers->triangles.cbegin() + meshlet.triangle_offset,
        cluster.buffers->triangles.cbegin() + meshlet.triangle_offset + (meshlet.triangle_count * 3),
        std::back_inserter(group_indices),
        [&cluster, &meshlet](const auto& vertex_index) {
          return cluster.buffers->vertices[meshlet.vertex_offset + vertex_index];
        });
  }
  return std::move(group_indices);
}

[[nodiscard]] std::pair<std::vector<unsigned int>, float> simplify_group(
    const std::vector<unsigned int>& group_indices,
    const std::vector<float>& vertices,
    size_t vertex_count,
    size_t vertex_stride,
    size_t max_triangles,
    float simplify_target_index_count_threshold,
    float min_target_error,
    float max_target_error,
    float simplify_scale,
    float lod_scale) {
  // todo: maybe optional with attributes?
  std::vector<uint32_t> simplified_indices(group_indices.size());
  size_t target_index_count =
      // we want to reduce ~50% of tris
      (static_cast<size_t>(static_cast<float>(max_triangles * simplify_target_index_count_threshold)) * 3) * 2;
  float step_target_error = std::lerp(min_target_error, max_target_error, lod_scale) *
                            simplify_scale;  // range [0..1] todo: what should that be? probably should depend on lod?
  uint32_t simplification_options = meshopt_SimplifyLockBorder | meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute;
  float simplification_error = 0.0f;

  simplified_indices.resize(meshopt_simplify(
      simplified_indices.data(),
      group_indices.data(),
      group_indices.size(),
      vertices.data(),
      vertex_count,
      vertex_stride,
      target_index_count,
      step_target_error,
      simplification_options,
      &simplification_error));

  return std::make_pair(std::move(simplified_indices), simplification_error);
}

/*
[[nodiscard]] std::vector<Cluster> process_cluster_pool(const std::vector<Cluster>& clusters) {
  std::vector<Cluster> next{};
  next.reserve(clusters.size() / 2);

  const auto groups = group_clusters(meshlets, max_num_clusters_per_group);

  size_t num_new_meshlets = 0;
  size_t num_not_simplified = 0;
  const auto start_process_groups = std::chrono::high_resolution_clock::now();
  for (const auto& group : groups) {
    const auto group_indices = merge_group(meshlets, group, max_triangles);

    const auto simplified_indices = simplify_group(
        group_indices,
        vertices,
        vertex_count,
        vertex_stride,
        max_vertices,
        simplify_target_index_count_threshold,
        min_target_error,
        max_target_error,
        simplify_scale,
        lod_scale);

    // todo: optimize group? since we don't use scan should be fine without?

    const auto group_meshlets = build_meshlets(
        simplified_indices, vertices, vertex_count, vertex_stride, max_vertices, max_triangles, cone_weight);

    num_new_meshlets += group_meshlets.meshlets.size();

    if (group_meshlets.meshlets.size() >= max_num_clusters_per_group) {
      ++num_not_simplified;
    }
  }
  const auto end_process_groups = std::chrono::high_resolution_clock::now();
  printf(
      "process groups: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_process_groups - start_process_groups).count());

  printf(
      "num meshlets: lod 1: %i vs lod 0: %i; target: %i; not simplified: %i\n",
      int(num_new_meshlets),
      int(meshlets.meshlets.size()),
      int(meshlets.meshlets.size()) / 2,
      int(num_not_simplified));

  return next;
}
*/

void create_dag(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride) {
  const auto start_time = std::chrono::high_resolution_clock::now();

  if ((vertices.size() * sizeof(float)) % vertex_stride != 0) {
    throw std::runtime_error("invalid vertex stride");
  }
  const size_t vertex_count = (vertices.size() * sizeof(float)) / vertex_stride;

  // todo: should be params
  const size_t max_vertices = 255;
  const size_t max_triangles = 128;
  const float cone_weight = 0.5;  // 0.5f;
  const size_t max_num_clusters_per_group = 4;
  const float simplify_target_index_count_threshold = 0.5f;
  const size_t max_lod_count = 25;
  const float min_target_error = 1e-2f;
  const float max_target_error = 1.0f;

  float simplify_scale = meshopt_simplifyScale(vertices.data(), vertex_count, vertex_stride);

  std::vector<size_t> lod_offsets = {0};
  Meshlets meshlets =
      build_meshlets(indices, vertices, vertex_count, vertex_stride, max_vertices, max_triangles, cone_weight);

  meshlets.errors = std::vector<float>(meshlets.meshlets.size(), 0.0f);

  meshlets.bounds = std::vector<meshopt_Bounds>(meshlets.meshlets.size());
  const auto start_meshlet_bounds = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < meshlets.meshlets.size(); ++i) {
    meshlets.bounds[i] = meshopt_computeMeshletBounds(
        &meshlets.vertices[meshlets.meshlets[i].vertex_offset],
        &meshlets.triangles[meshlets.meshlets[i].triangle_offset],
        meshlets.meshlets[i].triangle_count,
        vertices.data(),
        vertex_count,
        vertex_stride);
  }
  const auto end_meshlet_bounds = std::chrono::high_resolution_clock::now();
  /*
  printf(
      "meshlet bounds: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_meshlet_bounds - start_meshlet_bounds).count());
  */

  std::vector<Cluster> clusters(meshlets.meshlets.size());
  for (size_t i = 0; i < clusters.size(); ++i) {
    clusters[i] = Cluster{
        .index = i,
        .buffers = &meshlets,
    };
  }

  for (size_t level = 1; level < max_lod_count; ++level) {
    if (clusters.size() <= 1) {
      break;
    }
    lod_offsets.emplace_back(meshlets.meshlets.size());

    bool is_last = clusters.size() <= max_num_clusters_per_group;

    const float lod_scale = is_last ? 1.0 : static_cast<float>(level) / static_cast<float>(max_lod_count);
    const auto groups =
        is_last ? build_final_cluster_group(clusters.size()) : group_clusters(clusters, max_num_clusters_per_group);

    std::atomic_size_t num_new_meshlets = 0;
    std::atomic_size_t num_new_vertices = 0;
    std::atomic_size_t num_new_triangles = 0;
    std::atomic_size_t num_next_clusters = 0;
    std::atomic_size_t num_not_simplified = 0;

    std::vector<Meshlets> lod_meshlets(groups.size());
    std::vector<std::vector<Cluster>> lod_clusters(groups.size());
    const auto start_process_groups = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < groups.size(); ++i) {
      const auto& group = groups[i];
      const auto group_indices = merge_group(clusters, groups[i], max_triangles);

      const auto [simplified_indices, error] = simplify_group(
          group_indices,
          vertices,
          vertex_count,
          vertex_stride,
          max_triangles,
          simplify_target_index_count_threshold,
          min_target_error,
          max_target_error,
          simplify_scale,
          lod_scale);

      if (simplified_indices.size() >= group_indices.size()) {
        num_not_simplified += groups[i].size();
        std::transform(
            group.cbegin(), group.cend(), std::back_inserter(lod_clusters[i]), [&clusters](const size_t cluster_index) {
              return clusters[cluster_index];
            });
        num_next_clusters += group.size();
      } else {
        auto group_meshlets = std::move(build_meshlets(
            simplified_indices,
            vertices,
            vertex_count,
            vertex_stride,
            max_vertices,
            max_triangles,
            cone_weight,
            group.size() - 1));

        if (group_meshlets.meshlets.size() >= group.size()) {
          num_not_simplified += groups[i].size();
          std::transform(
              group.cbegin(),
              group.cend(),
              std::back_inserter(lod_clusters[i]),
              [&clusters](const size_t cluster_index) { return clusters[cluster_index]; });
          num_next_clusters += group.size();
        } else {
          num_new_meshlets += group_meshlets.meshlets.size();
          num_new_vertices += group_meshlets.vertices.size();
          num_new_triangles += group_meshlets.triangles.size();
          num_next_clusters += group_meshlets.meshlets.size();

          lod_meshlets[i] = std::move(group_meshlets);
          for (size_t cluster_index = 0; cluster_index < lod_meshlets[i].meshlets.size(); ++cluster_index) {
            // todo: each cluster is a parent of each cluster in the group
            lod_clusters[i].emplace_back(Cluster{
                .index = cluster_index,
                .buffers = &meshlets,
            });
          }
        }
      }
    }
    const auto end_process_groups = std::chrono::high_resolution_clock::now();
    /*
    printf(
        "process groups: took %ld ms\n",
        std::chrono::duration_cast<std::chrono::milliseconds>(end_process_groups - start_process_groups).count());
    */

    std::vector<Cluster> next_clusters{};
    // merge meshlets & prepare next iteration's clusters
    {
      next_clusters.reserve(num_next_clusters);

      meshlets.meshlets.reserve(meshlets.meshlets.size() + num_new_meshlets);
      meshlets.vertices.reserve(meshlets.vertices.size() + num_new_vertices);
      meshlets.triangles.reserve(meshlets.triangles.size() + num_new_triangles);

      for (size_t i = 0; i < groups.size(); ++i) {
        if (!lod_meshlets[i].meshlets.empty()) {
          for (size_t cluster_index = 0; cluster_index < lod_clusters[i].size(); ++cluster_index) {
            lod_clusters[i][cluster_index].index += meshlets.meshlets.size();
            lod_meshlets[i].meshlets[cluster_index].vertex_offset += meshlets.vertices.size();
            lod_meshlets[i].meshlets[cluster_index].triangle_offset += meshlets.triangles.size();
          }
          meshlets.meshlets.insert(
              meshlets.meshlets.end(),
              std::make_move_iterator(lod_meshlets[i].meshlets.begin()),
              std::make_move_iterator(lod_meshlets[i].meshlets.end()));
          meshlets.vertices.insert(
              meshlets.vertices.end(),
              std::make_move_iterator(lod_meshlets[i].vertices.begin()),
              std::make_move_iterator(lod_meshlets[i].vertices.end()));
          meshlets.triangles.insert(
              meshlets.triangles.end(),
              std::make_move_iterator(lod_meshlets[i].triangles.begin()),
              std::make_move_iterator(lod_meshlets[i].triangles.end()));
        }
        next_clusters.insert(
            next_clusters.end(),
            std::make_move_iterator(lod_clusters[i].begin()),
            std::make_move_iterator(lod_clusters[i].end()));
      }

      printf(
          "num meshlets: lod %i: %i vs lod %i: %i; target: %i; not simplified: %i\n",
          int(level),
          int(num_new_meshlets),
          int(level - 1),
          int(clusters.size()),
          int(clusters.size()) / 2,
          int(num_not_simplified));
    }

    // todo:
    // insert into dag

    clusters = std::move(next_clusters);
  }

  // parallelize, optimize runtime + memory usage, tune

  // todo: how to pack as gltf (?) (not part of the lib but for my personal use)
  //  - meshlet data in buffers (bounds, normal cone, error), dag?
  //  - each lod as a mesh? store lod hierarchy info in extras
  //  - each meshlet as a primitive? store hierarchy info (child indices, etc.) and culling (cluster bounds, normal cone, error) in extras

  const auto end_time = std::chrono::high_resolution_clock::now();
  printf(
      "create dag: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
}

}  // namespace pmn
