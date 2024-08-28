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

struct ClusterBounds {
  float center[3];
  float radius;
  float cone_axis[3];
  float cone_cutoff; /* = cos(angle/2) */
  float cone_apex[3];
  float error;
};

struct DagNode {
  size_t clusterIndex = 0;
  size_t level = 0;
  std::vector<size_t> children{};
  ClusterBounds bounds{};
};

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

  float simplify_scale = meshopt_simplifyScale(vertices.data(), vertex_count, vertex_stride);

  std::vector<DagNode> dagNodes{};

  std::vector<size_t> lod_offsets = {0};
  MeshletsBuffers meshlets =
      build_meshlets(indices, vertices, vertex_count, vertex_stride, max_vertices, max_triangles, cone_weight);

  std::vector<Cluster> clusters(meshlets.meshlets.size());
  dagNodes.reserve(clusters.size());
  for (size_t i = 0; i < clusters.size(); ++i) {
    const auto bounds = meshopt_computeMeshletBounds(
        &meshlets.vertices[meshlets.meshlets[i].vertex_offset],
        &meshlets.triangles[meshlets.meshlets[i].triangle_offset],
        meshlets.meshlets[i].triangle_count,
        vertices.data(),
        vertex_count,
        vertex_stride);

    auto& dagNode = dagNodes.emplace_back();
    dagNode.clusterIndex = i;
    dagNode.level = 0;
    dagNode.bounds.radius = bounds.radius;
    dagNode.bounds.cone_cutoff = bounds.cone_cutoff;
    for (size_t c = 0; c < 3; ++c) {
      dagNode.bounds.center[c] = bounds.center[c];
      dagNode.bounds.cone_apex[c] = bounds.center[c];
      dagNode.bounds.cone_axis[c] = bounds.center[c];
    }
    dagNode.bounds.error = 0.0;

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

    std::vector<MeshletsBuffers> lod_meshlets(groups.size());
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
      if (!simplified) {
        num_not_simplified += groups[i].size();
        std::transform(
            group.cbegin(), group.cend(), std::back_inserter(lod_clusters[i]), [&clusters](const size_t cluster_index) {
              return clusters[cluster_index];
            });
        num_next_clusters += group.size();
      }
    }
    const auto end_process_groups = std::chrono::high_resolution_clock::now();

    dagNodes.reserve(dagNodes.size() + num_new_meshlets);

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
