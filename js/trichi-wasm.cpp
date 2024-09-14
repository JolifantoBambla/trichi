#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "trichi.hpp"

[[nodiscard]] emscripten::val buildTriangleClusterHierarchy(const emscripten::val& indicesJs, const emscripten::val& verticesJs, const size_t vertexStride, const trichi::TriChiParams& params) {
  const auto hierarchy = trichi::build_cluster_hierarchy(
      emscripten::convertJSArrayToNumberVector<uint32_t>(indicesJs),
      emscripten::convertJSArrayToNumberVector<float>(verticesJs),
      vertexStride,
      params);

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

  auto bounds = emscripten::val::global("Float32Array").new_(hierarchy.bounds.size() * 4);
  for (size_t i = 0; i < hierarchy.bounds.size(); ++i) {
    bounds.set(i * 4 + 0, hierarchy.bounds[i].center[0]);
    bounds.set(i * 4 + 1, hierarchy.bounds[i].center[1]);
    bounds.set(i * 4 + 2, hierarchy.bounds[i].center[2]);
    bounds.set(i * 4 + 3, hierarchy.bounds[i].radius);
  }

  auto clusters = emscripten::val::global("Uint32Array").new_(hierarchy.clusters.size() * 4);
  for (size_t i = 0; i < hierarchy.clusters.size(); ++i) {
    clusters.set(i * 4 + 0, hierarchy.clusters[i].vertex_offset);
    clusters.set(i * 4 + 1, hierarchy.clusters[i].triangle_offset);
    clusters.set(i * 4 + 2, hierarchy.clusters[i].vertex_count);
    clusters.set(i * 4 + 3, hierarchy.clusters[i].triangle_count);
  }

  auto vertices = emscripten::val::global("Uint32Array").new_(hierarchy.vertices.size());
  for (size_t i = 0; i < hierarchy.vertices.size(); ++i) {
    vertices.set(i, hierarchy.vertices[i]);
  }

  auto triangles = emscripten::val::global("Uint8Array").new_(hierarchy.triangles.size());
  for (size_t i = 0; i < hierarchy.triangles.size(); ++i) {
    triangles.set(i, hierarchy.triangles[i]);
  }

  // todo: nodes & root nodes

  emscripten::val result = emscripten::val::object();
  result.set("errors", errors);
  result.set("bounds", bounds);
  result.set("clusters", clusters);
  result.set("vertices", vertices);
  result.set("triangles", triangles);

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
