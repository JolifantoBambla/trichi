//
// Created by lukas on 21.08.24.
//

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>  // todo: remove
#include <numeric>
#include <optional>
#include <thread>

#include "meshoptimizer.h"
#include "metis.h"

#include "impl.hpp"
#include "per_meshlet_nuances.hpp"

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

[[nodiscard]] MeshletsBuffers build_meshlets(
    const std::vector<uint32_t>& indices,
    const std::vector<float>& vertices,
    size_t vertex_count,
    size_t vertex_stride,
    size_t max_vertices,
    size_t max_triangles,
    float cone_weight,
    std::optional<size_t> max_allowed_meshlets = std::nullopt) {
  size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);
  MeshletsBuffers meshlets = {
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
  const float cone_weight = 0.5;
  const size_t max_num_clusters_per_group = 4;
  const float simplify_target_index_count_threshold = 0.5f;
  const size_t max_lod_count = 25;
  const float min_target_error = 1e-2f;
  const float max_target_error = 1.0f;

  LoopRunner loop_runner{std::thread::hardware_concurrency()};

  float simplify_scale = meshopt_simplifyScale(vertices.data(), vertex_count, vertex_stride);

  std::vector<size_t> lod_offsets = {0};
  std::vector<MeshletsBuffers> lods = {
      build_meshlets(indices, vertices, vertex_count, vertex_stride, max_vertices, max_triangles, cone_weight)};

  std::vector<Cluster> clusters(lods[0].meshlets.size());
  std::vector<DagNode> dagNodes(lods[0].meshlets.size());
  for (size_t i = 0; i < clusters.size(); ++i) {
    clusters[i] = Cluster{
        .index = i,
        .buffers = &lods[0],
        .dag_index = i,
    };
    init_dag_node(clusters[i], dagNodes[i], vertices, vertex_count, vertex_stride, i, 0, 0.0);
  }

  for (size_t level = 1; level < max_lod_count; ++level) {
    if (clusters.size() <= 1) {
      break;
    }
    lod_offsets.emplace_back(lods[0].meshlets.size());

    bool is_last = clusters.size() <= max_num_clusters_per_group;

    const float lod_scale = is_last ? 1.0f : static_cast<float>(level) / static_cast<float>(max_lod_count);
    const auto groups =
        is_last ? build_final_cluster_group(clusters.size()) : group_clusters(clusters, max_num_clusters_per_group, loop_runner);

    std::atomic_size_t num_new_meshlets = 0;
    std::atomic_size_t num_new_vertices = 0;
    std::atomic_size_t num_new_triangles = 0;
    std::atomic_size_t num_next_clusters = 0;
    std::atomic_size_t num_not_simplified = 0;

    std::vector<MeshletsBuffers> lod_meshlets(groups.size());
    std::vector<std::vector<Cluster>> lod_clusters(groups.size());
    std::vector<DagNode> lod_dag_nodes(groups.size() * 4);
    loop_runner.loop(0, groups.size(), [&](const size_t i) {
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

      bool simplified = simplified_indices.size() < group_indices.size();
      if (simplified) {
        auto group_meshlets = std::move(build_meshlets(
            simplified_indices,
            vertices,
            vertex_count,
            vertex_stride,
            max_vertices,
            max_triangles,
            cone_weight,
            group.size() - 1));

        simplified = group_meshlets.meshlets.size() < group.size();

        if (simplified) {
          size_t node_offset = num_new_meshlets.fetch_add(group_meshlets.meshlets.size());
          num_new_vertices += group_meshlets.vertices.size();
          num_new_triangles += group_meshlets.triangles.size();
          num_next_clusters += group_meshlets.meshlets.size();

          // the parent error can't be smaller than any error of its children for consistent lod selection
          float dag_node_error = std::accumulate(
              group.cbegin(),
              group.cend(),
              error,
              [&clusters, &dagNodes](const float max_error, const size_t cluster_index) {
                return std::max(max_error, dagNodes[clusters[cluster_index].dag_index].bounds.error);
              });

          std::vector<size_t> child_dag_indices(group.size());
          for (size_t child_index = 0; child_index < group.size(); ++child_index) {
            child_dag_indices[child_index] = clusters[group[child_index]].dag_index;
          }

          for (size_t cluster_index = 0; cluster_index < group_meshlets.meshlets.size(); ++cluster_index) {
            const size_t dag_node_index = node_offset + cluster_index;
            const auto& cluster = lod_clusters[i].emplace_back(Cluster{
                .index = cluster_index,
                .buffers = &group_meshlets,
                .dag_index = dag_node_index,
            });
            auto& node = lod_dag_nodes[dag_node_index];
            init_dag_node(cluster, node, vertices, vertex_count, vertex_stride, i, level, dag_node_error);
            node.children.insert(node.children.end(), child_dag_indices.cbegin(), child_dag_indices.cend());
          }

          lod_meshlets[i] = std::move(group_meshlets);
        }
      }
      if (!simplified) {
        num_not_simplified += groups[i].size();
        std::transform(
            group.cbegin(), group.cend(), std::back_inserter(lod_clusters[i]), [&clusters](const size_t cluster_index) {
              return clusters[cluster_index];
            });
        num_next_clusters += group.size();
      }
    });

    std::vector<Cluster> next_clusters{};
    // merge meshlets & prepare next iteration's clusters
    {
      next_clusters.reserve(num_next_clusters);

      auto& next_meshlets = lods.emplace_back();

      next_meshlets.meshlets.reserve(num_new_meshlets);
      next_meshlets.vertices.reserve(num_new_vertices);
      next_meshlets.triangles.reserve(num_new_triangles);

      for (size_t i = 0; i < groups.size(); ++i) {
        if (!lod_meshlets[i].meshlets.empty()) {
          for (size_t cluster_index = 0; cluster_index < lod_clusters[i].size(); ++cluster_index) {
            auto& cluster = lod_clusters[i][cluster_index];
            cluster.index += next_meshlets.meshlets.size();
            cluster.buffers = &next_meshlets;
            lod_dag_nodes[cluster.dag_index].clusterIndex = cluster.index;
            cluster.dag_index += dagNodes.size();

            lod_meshlets[i].meshlets[cluster_index].vertex_offset += next_meshlets.vertices.size();
            lod_meshlets[i].meshlets[cluster_index].triangle_offset += next_meshlets.triangles.size();
          }
          next_meshlets.meshlets.insert(
              next_meshlets.meshlets.end(),
              std::make_move_iterator(lod_meshlets[i].meshlets.begin()),
              std::make_move_iterator(lod_meshlets[i].meshlets.end()));
          next_meshlets.vertices.insert(
              next_meshlets.vertices.end(),
              std::make_move_iterator(lod_meshlets[i].vertices.begin()),
              std::make_move_iterator(lod_meshlets[i].vertices.end()));
          next_meshlets.triangles.insert(
              next_meshlets.triangles.end(),
              std::make_move_iterator(lod_meshlets[i].triangles.begin()),
              std::make_move_iterator(lod_meshlets[i].triangles.end()));
        }
        next_clusters.insert(
            next_clusters.end(),
            std::make_move_iterator(lod_clusters[i].begin()),
            std::make_move_iterator(lod_clusters[i].end()));
      }

      dagNodes.reserve(dagNodes.size() + num_new_meshlets);
      dagNodes.insert(
          dagNodes.end(),
          std::make_move_iterator(lod_dag_nodes.begin()),
          std::make_move_iterator(lod_dag_nodes.begin() + static_cast<off_t>(num_new_meshlets)));

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

  const auto root = dagNodes.back();
  
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
