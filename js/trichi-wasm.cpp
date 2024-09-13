#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "trichi.hpp"

[[nodiscard]] emscripten::val buildTriangleClusterHierarchy(const emscripten::val& indicesJs, const emscripten::val& verticesJs, const size_t vertexStride, const trichi::TriChiParams& params) {
  const auto indices = emscripten::convertJSArrayToNumberVector<uint32_t>(indicesJs);
  const auto vertices = emscripten::convertJSArrayToNumberVector<float>(vertices);

  const auto hierarchy = trichi::build_cluster_hierarchy(indices, vertices, vertexStride, params);

  emscripten::val view{ emscripten::typed_memory_view(hierarchy.errors.size() * 10, hierarchy.errors.data()) };

  auto errors = emscripten::val::global("Float32Array").new_(hierarchy.errors.size() * 10);
  for (size_t i = 0; i < hierarchy.errors.size(); ++i) {
    errors.set(i * 10 + 0, hierarchy.errors[i].parent_error.center[0]);
    errors.set(i * 10 + 1, hierarchy.errors[i].parent_error.center[1]);
    errors.set(i * 10 + 2, hierarchy.errors[i].parent_error.center[2]);
    errors.set(i * 10 + 3, hierarchy.errors[i].parent_error.radius);
    errors.set(i * 10 + 4, hierarchy.errors[i].parent_error.error);
    errors.set(i * 10 + 5, hierarchy.errors[i].cluster_error.center[0]);
    errors.set(i * 10 + 6, hierarchy.errors[i].cluster_error.center[1]);
    errors.set(i * 10 + 7, hierarchy.errors[i].cluster_error.center[2]);
    errors.set(i * 10 + 8, hierarchy.errors[i].cluster_error.radius);
    errors.set(i * 10 + 9, hierarchy.errors[i].cluster_error.error);
  }

  // todo: nodes, root nodes, bounds, clusters, vertices, triangles

  emscripten::val result = emscripten::val::object();
  result.set("errors", errors);

  return result;
}

EMSCRIPTEN_BINDINGS(trichi) {
  emscripten::value_object<trichi::TriChiParams>("TriChiParams")
    .field("maxVerticesPerCluster", &trichi::TriChiParams::max_vertices_per_cluster)
    .field("maxTrianglesPerCluster", &trichi::TriChiParams::max_triangles_per_cluster)
    .field("clusterConeWeight", &trichi::TriChiParams::cluster_cone_weight)
    .field("targetClustersPerGroup", &trichi::TriChiParams::target_clusters_per_group)
    .field("maxHierarchyDepth", &trichi::TriChiParams::max_hierarchy_depth)
    .field("threadPoolSize", &trichi::TriChiParams::thread_pool_size)
    .field("storeHierarchy", &trichi::TriChiParams::store_hierarchy);

  emscripten::function("buildTriangleClusterHierarchy", &buildTriangleClusterHierarchy);
}
