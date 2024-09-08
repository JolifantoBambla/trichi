//
// Created by lukas on 21.08.24.
//

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>  // todo: remove
#include <numeric>
#include <optional>
#include <thread>

#include "meshoptimizer.h"
#include "metis.h"

#include "impl.hpp"
#include "trichi.hpp"

namespace trichi {

[[nodiscard]] MeshletsBuffers build_meshlets(
    const std::vector<uint32_t>& indices,
    const std::vector<float>& vertices,
    size_t vertex_count,
    size_t vertex_stride,
    size_t max_vertices,
    size_t max_triangles,
    float cone_weight) {
  size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);
  MeshletsBuffers meshlets = {
      .meshlets = std::vector<meshopt_Meshlet>(max_meshlets),
      .vertices = std::vector<unsigned int>(max_meshlets * max_vertices),
      .triangles = std::vector<unsigned char>(max_meshlets * max_triangles * 3),
  };

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

  const auto& last = meshlets.meshlets.back();
  meshlets.vertices.resize(last.vertex_offset + last.vertex_count);
  meshlets.triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
  meshlets.bounds.resize(meshlets.meshlets.size());

  return std::move(meshlets);
}

[[nodiscard]] MeshletsBuffers build_parent_meshlets(
    const std::vector<uint32_t>& indices,
    const std::vector<float>& vertices,
    size_t vertex_count,
    size_t vertex_stride,
    size_t max_vertices,
    size_t max_triangles,
    float cone_weight,
    size_t max_meshlets) {
  auto meshlets = build_meshlets(indices, vertices, vertex_count, vertex_stride, max_vertices, max_triangles, cone_weight);

  if (meshlets.meshlets.size() <= max_meshlets) {
    for (size_t i = 0; i < meshlets.meshlets.size(); ++i) {
      const auto& meshlet = meshlets.meshlets[i];
      meshopt_optimizeMeshlet(
          &meshlets.vertices[meshlet.vertex_offset],
          &meshlets.triangles[meshlet.triangle_offset],
          meshlet.triangle_count,
          meshlet.vertex_count);

      meshlets.bounds[i] = meshopt_computeMeshletBounds(
          &meshlets.vertices[meshlet.vertex_offset],
          &meshlets.triangles[meshlet.triangle_offset],
          meshlet.triangle_count,
          vertices.data(),
          vertex_count,
          vertex_stride);
    }
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
merge_group(const std::vector<ClusterIndex>& clusters, const MeshletsBuffers& lods, const std::vector<size_t>& group, size_t max_triangles) {
  std::vector<uint32_t> group_indices{};
  group_indices.reserve(3 * max_triangles * group.size());
  for (const auto& meshlet_index : group) {
    const auto& cluster = clusters[meshlet_index];
    const auto& meshlet = lods.meshlets[cluster.index];
    if (meshlet.triangle_offset + (meshlet.triangle_count * 3) > lods.triangles.size()) {
      throw std::runtime_error("fuck");
    }
    std::transform(
        lods.triangles.cbegin() + meshlet.triangle_offset,
        lods.triangles.cbegin() + meshlet.triangle_offset + (meshlet.triangle_count * 3),
        std::back_inserter(group_indices),
        [&lods, &meshlet](const auto& vertex_index) {
          return lods.vertices[meshlet.vertex_offset + vertex_index];
        });
  }
  return std::move(group_indices);
}

[[nodiscard]] std::pair<std::vector<unsigned int>, float> simplify_group(
    const std::vector<unsigned int>& group_indices,
    const std::vector<float>& vertices,
    size_t vertex_count,
    size_t vertex_stride,
    size_t target_index_count,
    float target_error) {
  // todo: maybe optional with attributes?
  std::vector<uint32_t> simplified_indices(group_indices.size());
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
      target_error,
      simplification_options,
      &simplification_error));

  return std::make_pair(std::move(simplified_indices), simplification_error);
}

void build_cluster_hierarchy(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride, const TrichiParams& params) {
  const auto start_time = std::chrono::high_resolution_clock::now();

  if ((vertices.size() * sizeof(float)) % vertex_stride != 0) {
    throw std::runtime_error("invalid vertex stride");
  }
  const size_t vertex_count = (vertices.size() * sizeof(float)) / vertex_stride;

  const size_t max_vertices = params.max_vertices;
  const size_t max_triangles = params.max_triangles;
  const float cone_weight = params.cone_weight;
  const size_t max_num_clusters_per_group = params.max_num_clusters_per_group;
  const size_t simplify_target_index_count = std::min(max_vertices, max_triangles) * 3 * 2;
  const size_t max_lod_count = params.max_lod_count;

  LoopRunner loop_runner{std::thread::hardware_concurrency()};

  const float simplify_scale = meshopt_simplifyScale(vertices.data(), vertex_count, vertex_stride);

  std::vector<size_t> lod_offsets = {0};
  ClusterHierarchy dag{};


  MeshletsBuffers lods = build_meshlets(indices, vertices, vertex_count, vertex_stride, max_vertices, max_triangles, cone_weight);
  std::vector<DagNode> dagNodes(lods.meshlets.size());
  std::vector<NodeErrorBounds> node_error_bounds(lods.meshlets.size());
  std::vector<Node> nodes(lods.meshlets.size());

  std::vector<ClusterIndex> cluster_pool(lods.meshlets.size());

  loop_runner.loop(0, cluster_pool.size(), [&](size_t i) {
    const auto& meshlet = lods.meshlets[i];
    meshopt_optimizeMeshlet(
        &lods.vertices[meshlet.vertex_offset],
        &lods.triangles[meshlet.triangle_offset],
        meshlet.triangle_count,
        meshlet.vertex_count);
    lods.bounds[i] = meshopt_computeMeshletBounds(
        &lods.vertices[meshlet.vertex_offset],
        &lods.triangles[meshlet.triangle_offset],
        meshlet.triangle_count,
        vertices.data(),
        vertex_count,
        vertex_stride);

    node_error_bounds[i].cluster_error.center[0] = lods.bounds[i].center[0];
    node_error_bounds[i].cluster_error.center[1] = lods.bounds[i].center[1];
    node_error_bounds[i].cluster_error.center[2] = lods.bounds[i].center[2];
    node_error_bounds[i].cluster_error.radius = lods.bounds[i].radius;
    node_error_bounds[i].cluster_error.error = 0.0;

    cluster_pool[i] = ClusterIndex{
        .index = i,
        .lod = 0,
    };
  });

  for (size_t level = 1; level < max_lod_count; ++level) {
    const auto lod_start_time = std::chrono::high_resolution_clock::now();
    if (cluster_pool.size() <= 1) {
      break;
    }
    lod_offsets.emplace_back(lods.meshlets.size());

    bool is_last = cluster_pool.size() <= max_num_clusters_per_group;

    const auto groups = is_last ? build_final_cluster_group(cluster_pool.size())
                                : group_clusters(cluster_pool, lods, max_num_clusters_per_group, loop_runner);

    // todo: how should we set the target error? should this depend on lod?
    //const float lod_scale = is_last ? 1.0f : static_cast<float>(level) / static_cast<float>(max_lod_count);
    const float simplify_target_error = std::numeric_limits<float>::max(); // std::lerp(min_target_error, max_target_error, lod_scale) * simplify_scale;

    std::atomic_size_t num_new_meshlets = 0;
    std::atomic_size_t num_new_vertices = 0;
    std::atomic_size_t num_new_triangles = 0;
    std::atomic_size_t num_next_clusters = 0;
    std::atomic_size_t num_not_simplified = 0;

    std::vector<MeshletsBuffers> lod_meshlets(groups.size());
    std::vector<std::vector<ClusterIndex>> lod_clusters(groups.size());
    std::vector<DagNode> lod_dag_nodes(groups.size() * 4);
    loop_runner.loop(0, groups.size(), [&](const size_t i) {
      const auto& group = groups[i];
      if (group.empty()) {
        return;
      }

      bool simplified = group.size() != 1;
      if (simplified) {
        const auto group_indices = merge_group(cluster_pool, lods, group, max_triangles);

        const size_t target_index_count =
            group.size() <= 2 ? simplify_target_index_count / 2 : simplify_target_index_count;
        const auto [simplified_indices, error] = simplify_group(
            group_indices, vertices, vertex_count, vertex_stride, target_index_count, simplify_target_error);

        simplified = simplified_indices.size() < group_indices.size();
        if (simplified) {
          auto group_meshlets = std::move(build_parent_meshlets(
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

            // todo: error
            //  - all cluster_pool in group must have same sphere bounds & error
            //  - parent group sphere bounds must encompass all child vertices (=! group sphere bounds because 3d)

            ErrorBounds group_error_bounds = node_error_bounds[cluster_pool[group[0]].index].cluster_error;
            group_error_bounds.error = std::max(group_error_bounds.error, error);
            for (size_t j = 1; j < group.size(); ++j) {
              const auto& child_error = node_error_bounds[cluster_pool[group[j]].index].cluster_error;
              group_error_bounds.error = std::max(group_error_bounds.error, child_error.error);

              float dir[3] = {
                  child_error.center[0] - group_error_bounds.center[0],
                  child_error.center[1] - group_error_bounds.center[1],
                  child_error.center[2] - group_error_bounds.center[2],
              };
              float dist = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
              if ((dist + child_error.radius) > group_error_bounds.radius) {
                // todo: grow sphere
              }
            }

            // parent group error must be at least as large as the largest child error
            float dag_node_error = std::accumulate(
                group.cbegin(),
                group.cend(),
                error,
                [&cluster_pool, &node_error_bounds](const float max_error, const size_t cluster_index) {
                  return std::max(max_error, node_error_bounds[cluster_pool[cluster_index].index].cluster_error.error);
                });

            /*
            std::vector<size_t> child_dag_indices(group.size());
            for (size_t child_index = 0; child_index < group.size(); ++child_index) {
              child_dag_indices[child_index] = cluster_pool[group[child_index]].dag_index;
            }
             */

            for (size_t cluster_index = 0; cluster_index < group_meshlets.meshlets.size(); ++cluster_index) {
              const size_t dag_node_index = node_offset + cluster_index;
              const auto& cluster = lod_clusters[i].emplace_back(ClusterIndex{
                  .index = cluster_index,
                  .lod = level,
              });
              auto& node = lod_dag_nodes[dag_node_index];
              init_dag_node(
                  cluster, group_meshlets, node, vertices, vertex_count, vertex_stride, i, level, dag_node_error);
              //node.children.insert(node.children.end(), child_dag_indices.cbegin(), child_dag_indices.cend());
            }

            lod_meshlets[i] = std::move(group_meshlets);
          }
        }
      }
      if (!simplified) {
        num_not_simplified += group.size();
        num_next_clusters += group.size();
        std::transform(
            group.cbegin(), group.cend(), std::back_inserter(lod_clusters[i]), [&cluster_pool](const size_t cluster_index) {
              return cluster_pool[cluster_index];
            });
      }
    });

    std::vector<ClusterIndex> next_clusters{};
    // merge meshlets & prepare next iteration's cluster_pool
    {
      next_clusters.reserve(num_next_clusters);

      lods.meshlets.reserve(lods.meshlets.size() + num_new_meshlets);
      lods.vertices.reserve(lods.vertices.size() + num_new_vertices);
      lods.triangles.reserve(lods.triangles.size() + num_new_triangles);

      for (size_t i = 0; i < groups.size(); ++i) {
        if (!lod_meshlets[i].meshlets.empty()) {
          for (size_t cluster_index = 0; cluster_index < lod_clusters[i].size(); ++cluster_index) {
            auto& cluster = lod_clusters[i][cluster_index];
            if (cluster.lod != level) {
              continue;
            }
            cluster.index += lods.meshlets.size();

            lod_meshlets[i].meshlets[cluster_index].vertex_offset += lods.vertices.size();
            lod_meshlets[i].meshlets[cluster_index].triangle_offset += lods.triangles.size();
          }
          lods.meshlets.insert(
              lods.meshlets.cend(),
              std::make_move_iterator(lod_meshlets[i].meshlets.cbegin()),
              std::make_move_iterator(lod_meshlets[i].meshlets.cend()));
          lods.vertices.insert(
              lods.vertices.cend(),
              std::make_move_iterator(lod_meshlets[i].vertices.cbegin()),
              std::make_move_iterator(lod_meshlets[i].vertices.cend()));
          lods.triangles.insert(
              lods.triangles.cend(),
              std::make_move_iterator(lod_meshlets[i].triangles.cbegin()),
              std::make_move_iterator(lod_meshlets[i].triangles.cend()));
        }
        next_clusters.insert(
            next_clusters.cend(),
            std::make_move_iterator(lod_clusters[i].cbegin()),
            std::make_move_iterator(lod_clusters[i].cend()));
      }

      dagNodes.reserve(dagNodes.size() + num_new_meshlets);
      dagNodes.insert(
          dagNodes.cend(),
          std::make_move_iterator(lod_dag_nodes.cbegin()),
          std::make_move_iterator(lod_dag_nodes.cbegin() + static_cast<off_t>(num_new_meshlets)));

      printf(
          "num meshlets: lod %i: %i vs lod %i: %i; target: %i; not simplified: %i\n",
          int(level),
          int(num_new_meshlets),
          int(level - 1),
          int(cluster_pool.size()),
          int(cluster_pool.size()) / 2,
          int(num_not_simplified));
    }

    const auto lod_end_time = std::chrono::high_resolution_clock::now();
    printf(
        "lod %i: took %ld ms\n",
        int(level),
        std::chrono::duration_cast<std::chrono::milliseconds>(lod_end_time - lod_start_time).count());

    if (num_new_meshlets == 0) {
      break;
    }

    // todo:
    // insert into dag

    cluster_pool = std::move(next_clusters);
  }

  /*
  if (!lods.empty() && lods.back().meshlets.empty()) {
    lods.resize(lods.size() - 1);
  }
  size_t num_root_nodes = lods.back().meshlets.size();

  const auto root = dagNodes.back();
   */

  // parallelize, optimize runtime + memory usage, tune

  // todo: how to pack as gltf (?) (not part of the lib but for my personal use)
  //  - meshlet data in buffers (bounds, normal cone, error), dag?
  //  - each lod as a mesh? store lod hierarchy info in extras
  //  - each meshlet as a primitive? store hierarchy info (child indices, etc.) and culling (cluster bounds, normal cone, error) in extras

  const auto end_time = std::chrono::high_resolution_clock::now();
  printf(
      "create dag: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());

  // todo: for now to quickly have a demo
  //  - output all meshlet data as js file (concatenate meshlets, vertices, triangles)
  //  - output vertices in js file
  //  - output dag data in js file

  /*
  std::vector<size_t> offsets{};
  lod_offsets.reserve(lods.size());
  MeshletsBuffers concatenated{};
  for (size_t i = 0; i < lods.size(); ++i) {
    printf(
        "%i meshlets %i vertices %i triangles\n",
        int(lods[i].meshlets.size()),
        int(lods[i].vertices.size()),
        int(lods[i].triangles.size()));
    offsets.push_back(concatenated.meshlets.size());
    if (i == 0) {
      concatenated.meshlets.insert(concatenated.meshlets.cend(), lods[i].meshlets.cbegin(), lods[i].meshlets.cend());
    } else {
      std::transform(
          lods[i].meshlets.cbegin(), lods[i].meshlets.cend(), std::back_inserter(concatenated.meshlets), [&](auto m) {
            m.vertex_offset += concatenated.vertices.size();
            m.triangle_offset += concatenated.triangles.size();
            return m;
          });
    }
    concatenated.vertices.insert(concatenated.vertices.cend(), lods[i].vertices.cbegin(), lods[i].vertices.cend());
    concatenated.triangles.insert(concatenated.triangles.cend(), lods[i].triangles.cbegin(), lods[i].triangles.cend());
    printf(
        "concatenated: %i meshlets %i vertices %i triangles\n",
        int(concatenated.meshlets.size()),
        int(concatenated.vertices.size()),
        int(concatenated.triangles.size()));
  }

  float aabb_min[3] = {
      std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  float aabb_max[3] = {
      std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), std::numeric_limits<float>::min()};

  std::ofstream js_stream("/home/lherzberger/Projects/per-meshlet-nuances/demo/demo-mesh.js");
  js_stream << "export const mesh = {\n";
  js_stream << "  vertices: new Float32Array([";
  for (size_t i = 0; i < vertices.size(); ++i) {
    js_stream << vertices[i];
    if (i != vertices.size() - 1) {
      js_stream << ",";
    }
    if (i % 3 == 0) {
      for (size_t c = 0; c < 3; ++c) {
        aabb_min[c] = std::min(aabb_min[c], vertices[i + c]);
        aabb_max[c] = std::max(aabb_max[c], vertices[i + c]);
      }
    }
  }
  js_stream << "]),\n";

  js_stream << "  strideFloats: " << vertex_stride / sizeof(float) << ",\n";

  js_stream << "  meshlets: new Uint32Array([";
  for (size_t i = 0; i < concatenated.meshlets.size(); ++i) {
    if (i < lods[0].meshlets.size()) {
      assert(concatenated.meshlets[i].vertex_offset == lods[0].meshlets[i].vertex_offset);
      assert(concatenated.meshlets[i].triangle_offset == lods[0].meshlets[i].triangle_offset);
      assert(concatenated.meshlets[i].vertex_count == lods[0].meshlets[i].vertex_count);
      assert(concatenated.meshlets[i].triangle_count == lods[0].meshlets[i].triangle_count);
    }
    js_stream << concatenated.meshlets[i].vertex_offset << ",";
    js_stream << concatenated.meshlets[i].triangle_offset << ",";
    js_stream << concatenated.meshlets[i].vertex_count << ",";
    js_stream << concatenated.meshlets[i].triangle_count;
    if (i != concatenated.meshlets.size() - 1) {
      js_stream << ",";
    }
  }
  js_stream << "]),\n";

  js_stream << "  meshletVertices: new Uint32Array([";
  for (size_t i = 0; i < concatenated.vertices.size(); ++i) {
    if (i < lods[0].vertices.size()) {
      assert(concatenated.vertices[i] == lods[0].vertices[i]);
    }
    js_stream << concatenated.vertices[i];
    if (i != concatenated.vertices.size() - 1) {
      js_stream << ",";
    }
  }
  js_stream << "]),\n";

  js_stream << "  meshletTriangles: new Uint32Array([";
  for (size_t i = 0; i < concatenated.triangles.size(); ++i) {
    if (i < lods[0].triangles.size()) {
      assert(concatenated.triangles[i] == lods[0].triangles[i]);
    }
    js_stream << static_cast<uint32_t>(concatenated.triangles[i]);
    if (i != concatenated.triangles.size() - 1) {
      js_stream << ",";
    }
  }
  js_stream << "]),\n";

  js_stream << "  lods: new Uint32Array([";
  for (size_t i = 0; i < offsets.size(); ++i) {
    js_stream << offsets[i];
    if (i != offsets.size() - 1) {
      js_stream << ",";
    }
  }
  js_stream << "]),\n";

  js_stream << "  numMeshlets: " << concatenated.meshlets.size() << ",\n";

  js_stream << "  maxClusterTriangles: " << max_triangles << ",\n";

  js_stream << "  aabb: {min: new Float32Array([" << aabb_min[0] << "," << aabb_min[1] << "," << aabb_min[2]
            << "]), max: new Float32Array([" << aabb_max[0] << "," << aabb_max[1] << "," << aabb_max[2] << "])},\n";

  // todo: dag data
  js_stream << "  bounds: new Float32Array([";
  for (size_t i = 0; i < dagNodes.size(); ++i) {
    const auto& node = dagNodes[i];
    js_stream << node.bounds.center[0] << "," << node.bounds.center[1] << "," << node.bounds.center[2] << ",";
    js_stream << node.bounds.radius << ",";
    js_stream << node.bounds.cone_axis[0] << "," << node.bounds.cone_axis[1] << "," << node.bounds.cone_axis[2] << ",";
    js_stream << node.bounds.error << ",";
    js_stream << node.bounds.cone_apex[0] << "," << node.bounds.cone_apex[1] << "," << node.bounds.cone_apex[2] << ",";
    js_stream << node.bounds.cone_cutoff;
    if (i < dagNodes.size() - 1) {
      js_stream << ",";
    }
  }
  js_stream << "]),\n";

  js_stream << "}" << std::endl;
  */
}

}  // namespace trichi
