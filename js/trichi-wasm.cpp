#include <iostream>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "trichi.hpp"

[[nodiscard]] emscripten::val convertToJsObjec(const trichi::ClusterHierarchy& hierarchy, const bool trianglesAsU32 = false) {
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

  auto triangles = emscripten::val::global(trianglesAsU32 ? "Uint32Array" : "Uint8Array").new_(hierarchy.triangles.size());
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

[[nodiscard]] emscripten::val buildTriangleClusterHierarchy(const emscripten::val& indicesJs, const emscripten::val& verticesJs, const size_t vertexStride, const trichi::TriChiParams& params) {
  return convertToJsObjec(trichi::build_cluster_hierarchy(
      emscripten::convertJSArrayToNumberVector<uint32_t>(indicesJs),
      emscripten::convertJSArrayToNumberVector<float>(verticesJs),
      vertexStride,
      params));
}

[[nodiscard]] emscripten::val buildTriangleClusterHierarchyFromFileBlob(const std::string& fileName, const emscripten::val& bytesJs, const trichi::TriChiParams& params) {
  const size_t floats_per_vertex = 3;
  const size_t vertex_stride = floats_per_vertex * sizeof(float);
  std::vector<float> vertices{};
  std::vector<uint32_t> indices{};
  {
    const auto bytes = emscripten::convertJSArrayToNumberVector<uint8_t>(bytesJs);

    Assimp::Importer importer;
    const struct aiScene* scene = importer.ReadFileFromMemory(
        bytes.data(),
        bytes.size(),
        aiProcess_Triangulate | aiProcess_OptimizeGraph | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType,
        fileName.c_str());

    for (int i = 0; i < scene->mMeshes[0]->mNumVertices; ++i) {
      vertices.push_back(scene->mMeshes[0]->mVertices[i].x);
      vertices.push_back(scene->mMeshes[0]->mVertices[i].y);
      vertices.push_back(scene->mMeshes[0]->mVertices[i].z);
    }
    for (int i = 0; i < scene->mMeshes[0]->mNumFaces; ++i) {
      if (scene->mMeshes[0]->mFaces[i].mNumIndices != 3) {
        throw std::runtime_error("encountered non-triangle face");
      }
      for (int j = 0; j < scene->mMeshes[0]->mFaces[i].mNumIndices; ++j) {
        indices.push_back(scene->mMeshes[0]->mFaces[i].mIndices[j]);
      }
    }
  }
  std::cout << "Loaded model from memory\n";

  auto hierarchy = convertToJsObjec(trichi::build_cluster_hierarchy(indices, vertices, vertex_stride, params), true);

  std::cout << "Generated triangle cluster hierarchy\n";

  auto indicesJs = emscripten::val::global("Uint32Array").new_(indices.size());
  for (size_t i = 0; i < indices.size(); ++i) {
    indicesJs.set(i, indices[i]);
  }

  auto verticesJs = emscripten::val::global("Float32Array").new_(vertices.size() * 4);
  for (size_t i = 0; i < vertices.size(); ++i) {
    verticesJs.set(i, vertices[i]);
  }

  hierarchy.set("meshletVertices", hierarchy["vertices"]);
  hierarchy.set("meshletTriangles", hierarchy["triangles"]);
  hierarchy.set("indices", indicesJs);
  hierarchy.set("vertices", verticesJs);

  std::cout << "Processing done\n";

  return hierarchy;
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
  emscripten::function("buildTriangleClusterHierarchyFromFileBlob", &buildTriangleClusterHierarchyFromFileBlob);
}
