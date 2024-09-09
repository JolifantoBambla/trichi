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
      .clusters = {},
      .vertices = std::vector<unsigned int>(max_meshlets * max_vertices),
      .triangles = std::vector<unsigned char>(max_meshlets * max_triangles * 3),
  };

  std::vector<meshopt_Meshlet> mmeshlets{max_meshlets};

  // building the meshlets for lod 0 is the most time-consuming operation, guess there is not much more to do when it comes to performance optimization
  mmeshlets.resize(meshopt_buildMeshlets(
      mmeshlets.data(),
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

  // perf cost of this transform is insignificant
  std::transform(std::make_move_iterator(mmeshlets.cbegin()), std::make_move_iterator(mmeshlets.cend()), std::back_inserter(meshlets.clusters), [](const auto& meshlet) {
    return Cluster {
      .vertex_offset = meshlet.vertex_offset,
      .triangle_offset = meshlet.triangle_offset,
      .vertex_count = meshlet.vertex_count,
      .triangle_count = meshlet.triangle_count,
    };
  });

  const auto& last = meshlets.clusters.back();
  meshlets.vertices.resize(last.vertex_offset + last.vertex_count);
  meshlets.triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));

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

  if (meshlets.clusters.size() <= max_meshlets) {
    for (size_t i = 0; i < meshlets.clusters.size(); ++i) {
      const auto& meshlet = meshlets.clusters[i];
      meshopt_optimizeMeshlet(
          &meshlets.vertices[meshlet.vertex_offset],
          &meshlets.triangles[meshlet.triangle_offset],
          meshlet.triangle_count,
          meshlet.vertex_count);
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
    const auto& meshlet = lods.clusters[cluster.index];
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

ClusterHierarchy build_cluster_hierarchy(const std::vector<uint32_t>& indices, const std::vector<float>& vertices, size_t vertex_stride, const TriChiParams& params) {
  const auto start_time = std::chrono::high_resolution_clock::now();

  if ((vertices.size() * sizeof(float)) % vertex_stride != 0) {
    throw std::runtime_error("invalid vertex stride");
  }
  const size_t vertex_count = (vertices.size() * sizeof(float)) / vertex_stride;

  const size_t max_vertices = params.max_vertices_per_cluster;
  const size_t max_triangles = params.max_triangles_per_cluster;
  const float cone_weight = params.cluster_cone_weight;
  const size_t max_num_clusters_per_group = params.target_clusters_per_group;
  const size_t simplify_target_index_count = std::min(max_vertices, max_triangles) * 3 * 2;
  const size_t max_lod_count = params.max_hierarchy_depth;

  LoopRunner loop_runner{std::thread::hardware_concurrency()};

  //const float simplify_scale = meshopt_simplifyScale(vertices.data(), vertex_count, vertex_stride);

  std::vector<size_t> lod_offsets = {0};


  MeshletsBuffers lods = build_meshlets(indices, vertices, vertex_count, vertex_stride, max_vertices, max_triangles, cone_weight);
  std::vector<NodeErrorBounds> node_error_bounds(lods.clusters.size());
  std::vector<Node> nodes(lods.clusters.size());

  std::vector<ClusterIndex> cluster_pool(lods.clusters.size());

  loop_runner.loop(0, cluster_pool.size(), [&](size_t i) {
    const auto& meshlet = lods.clusters[i];
    meshopt_optimizeMeshlet(
        &lods.vertices[meshlet.vertex_offset],
        &lods.triangles[meshlet.triangle_offset],
        meshlet.triangle_count,
        meshlet.vertex_count);

    const auto cluster_bounds = meshopt_computeMeshletBounds(
        &lods.vertices[meshlet.vertex_offset],
        &lods.triangles[meshlet.triangle_offset],
        meshlet.triangle_count,
        vertices.data(),
        vertex_count,
        vertex_stride);

    node_error_bounds[i].cluster_error.center[0] = cluster_bounds.center[0];
    node_error_bounds[i].cluster_error.center[1] = cluster_bounds.center[1];
    node_error_bounds[i].cluster_error.center[2] = cluster_bounds.center[2];
    node_error_bounds[i].cluster_error.radius = cluster_bounds.radius;
    node_error_bounds[i].cluster_error.error = 0.0;
    node_error_bounds[i].normal_cone.apex[0] = cluster_bounds.cone_apex[0];
    node_error_bounds[i].normal_cone.apex[1] = cluster_bounds.cone_apex[1];
    node_error_bounds[i].normal_cone.apex[2] = cluster_bounds.cone_apex[2];
    node_error_bounds[i].normal_cone.axis[0] = cluster_bounds.cone_axis[0];
    node_error_bounds[i].normal_cone.axis[1] = cluster_bounds.cone_axis[1];
    node_error_bounds[i].normal_cone.axis[2] = cluster_bounds.cone_axis[2];

    cluster_pool[i] = ClusterIndex{
        .index = i,
        .lod = 0,
    };

    nodes[i] = Node{
        .cluster_index = i,
    };
  });

  for (size_t level = 1; level < max_lod_count; ++level) {
    const auto lod_start_time = std::chrono::high_resolution_clock::now();
    if (cluster_pool.size() <= 1) {
      break;
    }
    lod_offsets.emplace_back(lods.clusters.size());

    bool is_last = cluster_pool.size() <= max_num_clusters_per_group;

    const auto groups = is_last ? build_final_cluster_group(cluster_pool.size())
                                : group_clusters(cluster_pool, lods, max_num_clusters_per_group, loop_runner);

    const float simplify_target_error = std::numeric_limits<float>::max();

    std::atomic_size_t num_new_meshlets = 0;
    std::atomic_size_t num_new_vertices = 0;
    std::atomic_size_t num_new_triangles = 0;
    std::atomic_size_t num_next_clusters = 0;
    std::atomic_size_t num_not_simplified = 0;

    std::vector<MeshletsBuffers> lod_meshlets(groups.size());
    std::vector<std::vector<ClusterIndex>> lod_clusters(groups.size());
    std::vector<std::vector<NodeErrorBounds>> lod_error_bounds(groups.size());
    std::vector<std::vector<Node>> lod_nodes(groups.size());
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

          simplified = group_meshlets.clusters.size() < group.size();

          if (simplified) {
            num_new_meshlets += group_meshlets.clusters.size();
            num_new_vertices += group_meshlets.vertices.size();
            num_new_triangles += group_meshlets.triangles.size();
            num_next_clusters += group_meshlets.clusters.size();

            meshopt_Bounds group_bounds = meshopt_computeClusterBounds(
                group_indices.data(),
                group_indices.size(),
                vertices.data(),
                vertex_count,
                vertex_stride);

            ErrorBounds group_error_bounds{};
            group_error_bounds.error = std::accumulate(
                group.cbegin(),
                group.cend(),
                error,
                [&cluster_pool, &node_error_bounds](const float max_error, const size_t cluster_index) {
                  return std::max(max_error, node_error_bounds[cluster_pool[cluster_index].index].cluster_error.error);
                });
            group_error_bounds.center[0] = group_bounds.center[0];
            group_error_bounds.center[1] = group_bounds.center[1];
            group_error_bounds.center[2] = group_bounds.center[2];
            group_error_bounds.radius = group_bounds.radius;

            std::vector<size_t> child_node_indices(group.size());
            for (size_t child_index = 0; child_index < group_meshlets.clusters.size(); ++child_index) {
              const size_t cluster_index = group[child_index];
              // I'm not sure if I understood that correctly from the Nanite slides: the parent has to conservatively bound their children
              // but should its own cluster error then also be the group error? I don't think so because then cluster bounds would be way too large would they not?
              // I think clusters in the group only need to share the same parent bound but have their own bounds too

              // grow bounding sphere to enclose all its children's bounding spheres - this is not minimal anymore but should be good enough for lod decision?
              const auto& cluster_error = node_error_bounds[cluster_pool[cluster_index].index].cluster_error;
              const float c1c2[3] = {
                  cluster_error.center[0] - group_error_bounds.center[0],
                  cluster_error.center[1] - group_error_bounds.center[1],
                  cluster_error.center[2] - group_error_bounds.center[2],
              };
              const float dist = std::sqrt(c1c2[0] * c1c2[0] + c1c2[1] * c1c2[1] + c1c2[2] * c1c2[2]);
              group_error_bounds.radius = std::max(group_error_bounds.radius, dist + cluster_error.radius);

              child_node_indices[child_index] = cluster_pool[cluster_index].index;
            }

            for (const auto& child_index : group) {
              node_error_bounds[cluster_pool[child_index].index].parent_error = group_error_bounds;
            }

            for (size_t parent_index = 0; parent_index < group_meshlets.clusters.size(); ++parent_index) {
              const auto& cluster = group_meshlets.clusters[parent_index];
              const auto cluster_bounds = meshopt_computeMeshletBounds(
                  &group_meshlets.vertices[cluster.vertex_offset],
                  &group_meshlets.triangles[cluster.triangle_offset],
                  cluster.triangle_count,
                  vertices.data(),
                  vertex_count,
                  vertex_stride);

              auto& node_bounds = lod_error_bounds[i].emplace_back();
              node_bounds.cluster_error.center[0] = cluster_bounds.center[0];
              node_bounds.cluster_error.center[1] = cluster_bounds.center[1];
              node_bounds.cluster_error.center[2] = cluster_bounds.center[2];
              node_bounds.cluster_error.radius = cluster_bounds.radius;
              node_bounds.cluster_error.error = error;
              node_bounds.normal_cone.apex[0] = cluster_bounds.cone_apex[0];
              node_bounds.normal_cone.apex[1] = cluster_bounds.cone_apex[1];
              node_bounds.normal_cone.apex[2] = cluster_bounds.cone_apex[2];
              node_bounds.normal_cone.axis[0] = cluster_bounds.cone_axis[0];
              node_bounds.normal_cone.axis[1] = cluster_bounds.cone_axis[1];
              node_bounds.normal_cone.axis[2] = cluster_bounds.cone_axis[2];

              lod_clusters[i].emplace_back(ClusterIndex{
                  .index = parent_index,
                  .lod = level,
              });

              lod_nodes[i].emplace_back(Node{
                  .cluster_index = parent_index,
                  .child_node_indices = child_node_indices,
              });
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
    // merge clusters & prepare next iteration's cluster_pool
    {
      next_clusters.reserve(num_next_clusters);

      lods.clusters.reserve(lods.clusters.size() + num_new_meshlets);
      lods.vertices.reserve(lods.vertices.size() + num_new_vertices);
      lods.triangles.reserve(lods.triangles.size() + num_new_triangles);

      nodes.reserve(nodes.size() + num_new_meshlets);

      for (size_t i = 0; i < groups.size(); ++i) {
        if (!lod_meshlets[i].clusters.empty()) {
          for (size_t cluster_index = 0; cluster_index < lod_clusters[i].size(); ++cluster_index) {
            auto& cluster = lod_clusters[i][cluster_index];
            if (cluster.lod != level) {
              continue;
            }
            cluster.index += lods.clusters.size();

            lod_meshlets[i].clusters[cluster_index].vertex_offset += lods.vertices.size();
            lod_meshlets[i].clusters[cluster_index].triangle_offset += lods.triangles.size();

            lod_nodes[i][cluster_index].cluster_index += lods.clusters.size();
          }
          lods.clusters.insert(
              lods.clusters.cend(),
              std::make_move_iterator(lod_meshlets[i].clusters.cbegin()),
              std::make_move_iterator(lod_meshlets[i].clusters.cend()));
          lods.vertices.insert(
              lods.vertices.cend(),
              std::make_move_iterator(lod_meshlets[i].vertices.cbegin()),
              std::make_move_iterator(lod_meshlets[i].vertices.cend()));
          lods.triangles.insert(
              lods.triangles.cend(),
              std::make_move_iterator(lod_meshlets[i].triangles.cbegin()),
              std::make_move_iterator(lod_meshlets[i].triangles.cend()));
          node_error_bounds.insert(
              node_error_bounds.cend(),
              std::make_move_iterator(lod_error_bounds[i].cbegin()),
              std::make_move_iterator(lod_error_bounds[i].cend()));
          nodes.insert(
              nodes.cend(),
              std::make_move_iterator(lod_nodes[i].cbegin()),
              std::make_move_iterator(lod_nodes[i].cend()));
        }
        next_clusters.insert(
            next_clusters.cend(),
            std::make_move_iterator(lod_clusters[i].cbegin()),
            std::make_move_iterator(lod_clusters[i].cend()));
      }

      printf(
          "num clusters: lod %i: %i vs lod %i: %i; target: %i; not simplified: %i\n",
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

    cluster_pool = std::move(next_clusters);
  }

  size_t num_root_nodes = lods.clusters.size() - lod_offsets.back();
  std::vector<size_t> root_nodes{};
  for (size_t i = num_root_nodes; i < lods.clusters.size(); ++i) {
    root_nodes.emplace_back(i);
  }

  // todo: how to pack as gltf (?) (not part of the lib but for my personal use)
  //  - meshlet data in buffers (bounds, normal cone, error), dag?
  //  - each lod as a mesh? store lod hierarchy info in extras
  //  - each meshlet as a primitive? store hierarchy info (child indices, etc.) and culling (cluster bounds, normal cone, error) in extras

  const auto end_time = std::chrono::high_resolution_clock::now();
  printf(
      "create dag: took %ld ms\n",
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());

  // todo: for now to quickly have a demo
  //  - output all meshlet data as js file (concatenate clusters, vertices, triangles)
  //  - output vertices in js file
  //  - output dag data in js file

  /*
  std::vector<size_t> offsets{};
  lod_offsets.reserve(lods.size());
  MeshletsBuffers concatenated{};
  for (size_t i = 0; i < lods.size(); ++i) {
    printf(
        "%i clusters %i vertices %i triangles\n",
        int(lods[i].clusters.size()),
        int(lods[i].vertices.size()),
        int(lods[i].triangles.size()));
    offsets.push_back(concatenated.clusters.size());
    if (i == 0) {
      concatenated.clusters.insert(concatenated.clusters.cend(), lods[i].clusters.cbegin(), lods[i].clusters.cend());
    } else {
      std::transform(
          lods[i].clusters.cbegin(), lods[i].clusters.cend(), std::back_inserter(concatenated.clusters), [&](auto m) {
            m.vertex_offset += concatenated.vertices.size();
            m.triangle_offset += concatenated.triangles.size();
            return m;
          });
    }
    concatenated.vertices.insert(concatenated.vertices.cend(), lods[i].vertices.cbegin(), lods[i].vertices.cend());
    concatenated.triangles.insert(concatenated.triangles.cend(), lods[i].triangles.cbegin(), lods[i].triangles.cend());
    printf(
        "concatenated: %i clusters %i vertices %i triangles\n",
        int(concatenated.clusters.size()),
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

  js_stream << "  clusters: new Uint32Array([";
  for (size_t i = 0; i < concatenated.clusters.size(); ++i) {
    if (i < lods[0].clusters.size()) {
      assert(concatenated.clusters[i].vertex_offset == lods[0].clusters[i].vertex_offset);
      assert(concatenated.clusters[i].triangle_offset == lods[0].clusters[i].triangle_offset);
      assert(concatenated.clusters[i].vertex_count == lods[0].clusters[i].vertex_count);
      assert(concatenated.clusters[i].triangle_count == lods[0].clusters[i].triangle_count);
    }
    js_stream << concatenated.clusters[i].vertex_offset << ",";
    js_stream << concatenated.clusters[i].triangle_offset << ",";
    js_stream << concatenated.clusters[i].vertex_count << ",";
    js_stream << concatenated.clusters[i].triangle_count;
    if (i != concatenated.clusters.size() - 1) {
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

  js_stream << "  numMeshlets: " << concatenated.clusters.size() << ",\n";

  js_stream << "  maxClusterTriangles: " << max_triangles_per_cluster << ",\n";

  js_stream << "  aabb: {min: new Float32Array([" << aabb_min[0] << "," << aabb_min[1] << "," << aabb_min[2]
            << "]), max: new Float32Array([" << aabb_max[0] << "," << aabb_max[1] << "," << aabb_max[2] << "])},\n";

  // todo: dag data
  js_stream << "  bounds: new Float32Array([";
  for (size_t i = 0; i < dagNodes.size(); ++i) {
    const auto& node = dagNodes[i];
    js_stream << node.bounds.center[0] << "," << node.bounds.center[1] << "," << node.bounds.center[2] << ",";
    js_stream << node.bounds.radius << ",";
    js_stream << node.bounds.axis[0] << "," << node.bounds.axis[1] << "," << node.bounds.axis[2] << ",";
    js_stream << node.bounds.error << ",";
    js_stream << node.bounds.apex[0] << "," << node.bounds.apex[1] << "," << node.bounds.apex[2] << ",";
    js_stream << node.bounds.cutoff;
    if (i < dagNodes.size() - 1) {
      js_stream << ",";
    }
  }
  js_stream << "]),\n";

  js_stream << "}" << std::endl;
  */

  return ClusterHierarchy{
      .nodes = std::move(nodes),
      .root_nodes = std::move(root_nodes),
      .node_errors = std::move(node_error_bounds),
      .clusters = std::move(lods.clusters),
      .vertices = std::move(lods.vertices),
      .triangles = std::move(lods.triangles),
  };
}

}  // namespace trichi
