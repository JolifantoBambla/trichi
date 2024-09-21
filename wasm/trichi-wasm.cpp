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
    errors.set(i * 10 + 0, hierarchy.errors[i].parentError.center[0]);
    errors.set(i * 10 + 1, hierarchy.errors[i].parentError.center[1]);
    errors.set(i * 10 + 2, hierarchy.errors[i].parentError.center[2]);
    errors.set(i * 10 + 3, hierarchy.errors[i].parentError.radius);
    errors.set(i * 10 + 4, hierarchy.errors[i].parentError.error);
    errors.set(i * 10 + 5, hierarchy.errors[i].clusterError.center[0]);
    errors.set(i * 10 + 6, hierarchy.errors[i].clusterError.center[1]);
    errors.set(i * 10 + 7, hierarchy.errors[i].clusterError.center[2]);
    errors.set(i * 10 + 8, hierarchy.errors[i].clusterError.radius);
    errors.set(i * 10 + 9, hierarchy.errors[i].clusterError.error);
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
    clusters.set(i * 4 + 0, hierarchy.clusters[i].vertexOffset);
    clusters.set(i * 4 + 1, hierarchy.clusters[i].triangleOffset);
    clusters.set(i * 4 + 2, hierarchy.clusters[i].vertexCount);
    clusters.set(i * 4 + 3, hierarchy.clusters[i].triangleCount);
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
  result.set("clusterVertices", vertices);
  result.set("clusterTriangles", triangles);

  return result;
}

[[nodiscard]] emscripten::val buildTriangleClusterHierarchy(const emscripten::val& indicesJs, const emscripten::val& verticesJs, const size_t vertexStride, const trichi::Params& params) {
  auto hierarchy = convertToJsObjec(
      trichi::buildClusterHierarchy(
        emscripten::convertJSArrayToNumberVector<uint32_t>(indicesJs),
        emscripten::convertJSArrayToNumberVector<float>(verticesJs),
        vertexStride,
        params),
      true);
  hierarchy.set("indices", indicesJs);
  hierarchy.set("vertices", verticesJs);
  return hierarchy;
}

[[nodiscard]] emscripten::val buildTriangleClusterHierarchyFromFileBlob(const std::string& fileName, const emscripten::val& bytesJs, const trichi::Params& params) {
  const size_t floatsPerVertex = 3;
  const size_t vertexStride = floatsPerVertex * sizeof(float);
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

  auto hierarchy = convertToJsObjec(trichi::buildClusterHierarchy(indices, vertices, vertexStride, params), true);

  std::cout << "Generated triangle cluster hierarchy\n";

  auto indicesJs = emscripten::val::global("Uint32Array").new_(indices.size());
  for (size_t i = 0; i < indices.size(); ++i) {
    indicesJs.set(i, indices[i]);
  }

  auto verticesJs = emscripten::val::global("Float32Array").new_(vertices.size() * 4);
  for (size_t i = 0; i < vertices.size(); ++i) {
    verticesJs.set(i, vertices[i]);
  }

  hierarchy.set("indices", indicesJs);
  hierarchy.set("vertices", verticesJs);

  std::cout << "Processing done\n";

  return hierarchy;
}

EMSCRIPTEN_BINDINGS(trichi) {
  emscripten::value_object<trichi::Params>("Params")
    .field("maxVerticesPerCluster", &trichi::Params::maxVerticesPerCluster)
    .field("maxTrianglesPerCluster", &trichi::Params::maxTrianglesPerCluster)
    .field("clusterConeWeight", &trichi::Params::clusterConeWeight)
    .field("targetClustersPerGroup", &trichi::Params::targetClustersPerGroup)
    .field("maxHierarchyDepth", &trichi::Params::maxHierarchyDepth)
    .field("threadPoolSize", &trichi::Params::threadPoolSize);

  emscripten::function("buildTriangleClusterHierarchy", &buildTriangleClusterHierarchy);
  emscripten::function("buildTriangleClusterHierarchyFromFileBlob", &buildTriangleClusterHierarchyFromFileBlob);
}
