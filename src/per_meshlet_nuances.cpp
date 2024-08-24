//
// Created by lukas on 24.08.24.
//
//
// Created by lukas on 21.08.24.
//

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>  // todo: remove
#include <set>
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
// todo: figure out API, move impl to private files
void generateNextLod() {}

[[nodiscard]] std::array<idx_t, METIS_NOPTIONS> createPartitionOptions() {
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
  // todo: for the stanford bunny, I get an error if I force contiguous partitions saying that the input graph is not contiguous - isn't the bunny a manifold mesh and thus should be contiguous? maybe I also misunderstand the terminology? let's build it and then visualize the results to get a better understanding of what's happening
  options[METIS_OPTION_CONTIG] =
      0;  // 1 -> force contiguous partitions (I think that means that nodes in groups are connected? maybe not?)
  //options[METIS_OPTION_SEED] = 0; // seed for rng
  options[METIS_OPTION_NUMBERING] = 0;  // 0 -> result is 0-indexed
#ifndef NDEBUG
  options[METIS_OPTION_DBGLVL] |= METIS_DBG_INFO;
#endif
  // todo: add ways to set debug level options for other stuff, e.g., timing
  return options;
}

struct HighResMeshlets {
  std::vector<meshopt_Meshlet> meshlets;
  std::vector<unsigned int> vertices;
  std::vector<unsigned char> triangles;
};

void create_dag(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride) {
  const auto start_time = std::chrono::high_resolution_clock::now();

  if ((vertices.size() * sizeof(float)) % vertex_stride != 0) {
    throw std::runtime_error("invalid vertex stride");
  }
  const size_t vertex_count = (vertices.size() * sizeof(float)) / vertex_stride;

  // todo: should be params
  const size_t max_vertices = 255;
  const size_t max_triangles = 128;
  const float cone_weight = 0.5f;
  const size_t max_num_clusters_per_group = 4;
  const float simplify_target_index_count_threshold = 0.2f;

  float simplify_scale = meshopt_simplifyScale(vertices.data(), vertex_count, vertex_stride);

  // todo: maybe optimize mesh before generating meshlets? since we don't use scan should be fine without?

  size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);
  std::vector<meshopt_Meshlet> meshlets(max_meshlets);
  std::vector<unsigned int> meshlet_vertices(max_meshlets * max_vertices);
  std::vector<unsigned char> meshlet_triangles(max_meshlets * max_triangles * 3);

  const auto start_build_meshlets = std::chrono::high_resolution_clock::now();
  meshlets.resize(meshopt_buildMeshlets(
      meshlets.data(),
      meshlet_vertices.data(),
      meshlet_triangles.data(),
      indices.data(),
      indices.size(),
      vertices.data(),
      vertex_count,
      vertex_stride,
      max_vertices,
      max_triangles,
      cone_weight));
  const auto end_build_meshlets = std::chrono::high_resolution_clock::now();
  printf(
      "build meshlets: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_build_meshlets - start_build_meshlets).count());

  const meshopt_Meshlet& last = meshlets.back();
  meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
  meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));

  const auto start_optimize_meshlets = std::chrono::high_resolution_clock::now();
  for (const auto& meshlet : meshlets) {
    meshopt_optimizeMeshlet(
        &meshlet_vertices[meshlet.vertex_offset],
        &meshlet_triangles[meshlet.triangle_offset],
        meshlet.triangle_count,
        meshlet.vertex_count);
  }
  const auto end_optimize_meshlets = std::chrono::high_resolution_clock::now();
  printf(
      "optimize meshlets: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_optimize_meshlets - start_optimize_meshlets).count());

  // todo: clean meshlets: move triangles with one or less shared vertices to other meshlet

  std::vector<meshopt_Bounds> bounds{};
  bounds.reserve(meshlets.size());
  const auto start_meshlet_bounds = std::chrono::high_resolution_clock::now();
  for (const auto& meshlet : meshlets) {
    bounds.push_back(meshopt_computeMeshletBounds(
        &meshlet_vertices[meshlet.vertex_offset],
        &meshlet_triangles[meshlet.triangle_offset],
        meshlet.triangle_count,
        vertices.data(),
        vertex_count,
        vertex_stride));
  }
  const auto end_meshlet_bounds = std::chrono::high_resolution_clock::now();
  printf(
      "meshlet bounds: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_meshlet_bounds - start_meshlet_bounds).count());

  // compute boundary for each cluster
  std::vector<std::vector<uint64_t>> boundaries{};
  boundaries.reserve(meshlets.size());
  const auto start_meshlet_boundary = std::chrono::high_resolution_clock::now();
  for (const auto& meshlet : meshlets) {
    // find edges
    std::unordered_map<uint64_t, int> edges{};
    for (size_t i = 0; i < meshlet.triangle_count; ++i) {
      const uint64_t a = meshlet_triangles[meshlet.triangle_offset + i * 3 + 0];
      const uint64_t b = meshlet_triangles[meshlet.triangle_offset + i * 3 + 1];
      const uint64_t c = meshlet_triangles[meshlet.triangle_offset + i * 3 + 2];
      const uint64_t e1 = a < b ? (a << 32) | b : (b << 32) | a;
      const uint64_t e2 = a < c ? (a << 32) | c : (c << 32) | a;
      const uint64_t e3 = b < c ? (b << 32) | c : (c << 32) | b;
      if (!edges.contains(e1)) {
        edges[e1] = 1;
      } else {
        ++edges[e1];
      }
      if (!edges.contains(e2)) {
        edges[e2] = 1;
      } else {
        ++edges[e2];
      }
      if (!edges.contains(e3)) {
        edges[e3] = 1;
      } else {
        ++edges[e3];
      }
    }

    // find boundary = find edges that only appear once
    boundaries.emplace_back();
    auto& boundary = boundaries.back();
    for (const auto& [k, v] : edges) {
      if (v == 1) {
        boundary.push_back(k);
      }
    }
    std::sort(boundary.begin(), boundary.end());
  }
  const auto end_meshlet_boundary = std::chrono::high_resolution_clock::now();
  printf(
      "meshlet boundary: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_meshlet_boundary - start_meshlet_boundary).count());

  // compute neighborhood for each meshlet - store shared boundaries for computing group boundary later
  // compute metis inputs here as well (xadj is exclusive scan of node degrees, adjacency is list of neighboring node indices, adjwght is list of edge weights)
  // todo: currently, this is by far the slowest part (by a magnitude slower than partitioning) - parallelize & use less mem
  std::vector<std::unordered_map<size_t, std::vector<uint64_t>>> neighborhoods{};
  neighborhoods.reserve(meshlets.size());
  std::vector<idx_t> xadj = {0};
  xadj.reserve(meshlets.size() + 1);
  std::vector<idx_t> adjacency{};
  std::vector<idx_t> adjwght{};
  const auto start_build_meshlet_graph = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < boundaries.size(); ++i) {
    const auto& a = boundaries[i];
    neighborhoods.emplace_back();
    auto& neighborhood = neighborhoods.back();
    xadj.push_back(xadj[i - 1]);
    for (size_t j = 0; j < i; ++j) {
      if (neighborhoods[j].contains(i)) {
        ++xadj[i];
        adjacency.push_back(static_cast<int>(j));
        adjwght.push_back(static_cast<int>(neighborhoods[j][i].size()));
      }
    }
    for (size_t j = i + 1; j < boundaries.size(); ++j) {
      const auto& b = boundaries[j];
      std::vector<uint64_t> shared_boundary{};
      std::set_union(a.cbegin(), a.cend(), b.cbegin(), b.cend(), std::back_inserter(shared_boundary));
      if (!shared_boundary.empty()) {
        ++xadj[i];
        adjacency.push_back(static_cast<int>(j));
        adjwght.push_back(static_cast<int>(shared_boundary.size()));
        neighborhood[j] = std::move(shared_boundary);
      }
    }
  }
  const auto end_build_meshlet_graph = std::chrono::high_resolution_clock::now();
  printf(
      "meshlet graph: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_build_meshlet_graph - start_build_meshlet_graph)
          .count());

  // partition graph into groups of 4 clusters
  // todo: mt-kahypar (https://github.com/kahypar/mt-kahypar) looks very promising for graph partitioning - no static lib though, so needs some work for wasm build
  auto numVertices = static_cast<idx_t>(meshlets.size());
  idx_t numConstraints = 1;  // 1 is the minimum allowed value
  idx_t numParts = numVertices / static_cast<idx_t>(max_num_clusters_per_group);
  idx_t edgeCut = 0;
  std::vector<idx_t> partition = std::vector<idx_t>(numVertices, 0);
  std::array<idx_t, METIS_NOPTIONS> options = createPartitionOptions();

  const auto start_partition_meshlet_graph = std::chrono::high_resolution_clock::now();
  const auto partitionResult = METIS_PartGraphKway(
      &numVertices,      // number of vertices
      &numConstraints,   // number of constraints (=1 -> edge weights? or just 1 because that's the minimum?)
      xadj.data(),       // adjacency information: exclusive scan of vertex degrees
      adjacency.data(),  // adjacency information: list of edges per node
      nullptr,           // vertex weights
      nullptr,  // number of vertices for computing the total communication volume -> not used, we use min edge cut
      adjwght.data(),  // edge weights
      &numParts,  // number of groups -> we want to half the number of tris by grouping 4 clusters that are then simplified and then split in half
      nullptr,    // weight for each partition -> nullptr means we want an equally distributed partition
      nullptr,    // the allowed load imbalance tolerance -> we use the default
      options.data(),  // options
      &edgeCut,        // the edge cut (or total communication volume)
      partition
          .data());  // the partition vector of the graph (0- or 1-indexing depends on value of METIS_OPTION_NUMBERING)
  const auto end_partition_meshlet_graph = std::chrono::high_resolution_clock::now();
  printf(
      "graph partition: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_partition_meshlet_graph - start_partition_meshlet_graph)
          .count());

  if (partitionResult != METIS_OK) {
    if (partitionResult == METIS_ERROR_INPUT) {
      throw std::runtime_error("could not partition graph - input error");
    } else if (partitionResult == METIS_ERROR_MEMORY) {
      throw std::runtime_error("could not partition graph - not enough memory");
    } else {
      throw std::runtime_error("could not partition graph - unknown error");
    }
  }

  // merge clusters in group, simplify to ~50% tris, split into meshlets of max 128 tris
  auto groups = std::vector<std::vector<size_t>>(numParts);
  const auto start_parse_partition = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < meshlets.size(); ++i) {
    groups[partition[i]].push_back(i);
  }
  const auto end_parse_partition = std::chrono::high_resolution_clock::now();
  printf(
      "parse partition: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_parse_partition - start_parse_partition).count());

  const auto start_process_groups = std::chrono::high_resolution_clock::now();
  for (const auto& group : groups) {
    std::vector<uint32_t> group_indices{};
    group_indices.reserve(3 * max_triangles * max_num_clusters_per_group);

    // merge groups
    for (const auto& meshlet_index : group) {
      const auto& meshlet = meshlets[meshlet_index];
      std::transform(
          meshlet_triangles.cbegin() + meshlet.triangle_offset,
          meshlet_triangles.cbegin() + meshlet.triangle_offset + (meshlet.triangle_count * 3),
          std::back_inserter(group_indices),
          [&meshlet_vertices, &meshlet_triangles, &meshlet](const auto& vertex_index) {
            return meshlet_vertices[meshlet.vertex_offset + vertex_index];
          });
    }

    // todo: simplify group
    // todo: maybe optional with attributes?
    std::vector<uint32_t> simplified_indices(group_indices.size());
    size_t target_index_count =
        (static_cast<size_t>(static_cast<float>(max_vertices * simplify_target_index_count_threshold)) * 3) *
        2;  // we want to reduce ~50% of tris
    float target_error =
        1.0;  //1e-2f * simplify_scale; // range [0..1] todo: what should that be? probably should depend on lod?
    uint32_t simplification_options =
        meshopt_SimplifyLockBorder | meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute;
    float simplification_error = 0.0f;

    simplified_indices.resize(meshopt_simplify(
        simplified_indices.data(),
        group_indices.data(),
        group_indices.size(),
        vertices.data(),
        vertex_count,
        vertex_stride,
        target_index_count,
        target_error,
        simplification_options,
        &simplification_error));

    printf(
        "simplified has %i indices (%i tris) vs %i (%i tris) before simplify (%i target tri count, %i target index "
        "count, %f target error)\n",
        int(simplified_indices.size()),
        int(simplified_indices.size() / 3),
        int(group_indices.size()),
        int(group_indices.size() / 3),
        int(target_index_count / 3),
        int(target_index_count),
        target_error);

    // todo: optimize group? since we don't use scan should be fine without?

    size_t max_group_meshlets = meshopt_buildMeshletsBound(simplified_indices.size(), max_vertices, max_triangles);
    std::vector<meshopt_Meshlet> group_meshlet(max_group_meshlets);
    std::vector<unsigned int> group_meshlet_vertices(max_group_meshlets * max_vertices);
    std::vector<unsigned char> group_meshlet_triangles(max_group_meshlets * max_triangles * 3);

    group_meshlet.resize(meshopt_buildMeshlets(
        group_meshlet.data(),
        group_meshlet_vertices.data(),
        group_meshlet_triangles.data(),
        simplified_indices.data(),
        simplified_indices.size(),
        vertices.data(),
        vertex_count,
        vertex_stride,
        max_vertices,
        max_triangles,
        cone_weight));

    printf("next lod has %i meshlets\n", int(group_meshlet.size()));

    for (const auto& meshlet : group_meshlet) {
      meshopt_optimizeMeshlet(
          &group_meshlet_vertices[meshlet.vertex_offset],
          &group_meshlet_triangles[meshlet.triangle_offset],
          meshlet.triangle_count,
          meshlet.vertex_count);
    }
  }
  const auto end_process_groups = std::chrono::high_resolution_clock::now();
  printf(
      "process groups: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_process_groups - start_process_groups).count());

  // todo:
  // group up to n clusters based on number of shared boundary edges
  // simplify to 50% of tris in group
  // split groups into 2 clusters of n triangles each
  // repeat

  const auto end_time = std::chrono::high_resolution_clock::now();
  printf(
      "create dag: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
}
}  // namespace pmn
