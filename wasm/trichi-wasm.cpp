#include <iostream>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "meshoptimizer.h"
#include "trichi.hpp"

[[nodiscard]] emscripten::val convertToJsObject(const trichi::ClusterHierarchy& hierarchy, const bool trianglesAsU32 = false) {
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
  auto hierarchy = convertToJsObject(
      trichi::buildClusterHierarchy(
        emscripten::convertJSArrayToNumberVector<uint32_t>(indicesJs),
        emscripten::convertJSArrayToNumberVector<float>(verticesJs),
        vertexStride,
        params),
      true);
  hierarchy.set("indices", indicesJs);
  hierarchy.set("vertices", verticesJs);
  hierarchy.set("vertexStrideFloats", vertexStride / sizeof(float));
  return hierarchy;
}

[[nodiscard]] std::vector<uint8_t> toQuantizedVertexBytes(const std::vector<float>& vertices) {
  const auto vertexCount = vertices.size() / 6;

  std::vector<uint8_t> bytes{};
  bytes.reserve(vertexCount * (sizeof(uint16_t) * 3 + sizeof(int8_t) * 2));

  for (size_t i = 0; i < vertexCount; ++i) {
    const auto v = i * 6;

    for (size_t c = 0; c < 3; ++c) {
      const uint16_t comp = meshopt_quantizeHalf(vertices[v + c]);
      bytes.push_back(static_cast<uint8_t>(comp & 0xff));
      bytes.push_back(static_cast<uint8_t>((comp >> 8) & 0xff));
    }

    const struct {
      float x, y, z;
    } n = {vertices[v + 3], vertices[v + 4], vertices[v + 5],};
    float nsum = fabsf(n.x) + fabsf(n.y) + fabsf(n.z);
    float nx = n.x / nsum;
    float ny = n.y / nsum;

    int8_t nu = static_cast<int8_t>(meshopt_quantizeSnorm(n.z >= 0.0f ? nx : (1.0f - fabsf(ny)) * (nx >= 0.0f ? 1.0f : -1.0f), 8));
    int8_t nv = static_cast<int8_t>(meshopt_quantizeSnorm(n.z >= 0.0f ? ny : (1.0f - fabsf(nx)) * (ny >= 0.0f ? 1.0f : -1.0f), 8));

    bytes.push_back(static_cast<uint8_t>(nu));
    bytes.push_back(static_cast<uint8_t>(nv));
  }

  return std::move(bytes);
}

[[nodiscard]] emscripten::val buildTriangleClusterHierarchyFromFileBlob(const std::string& fileName, const emscripten::val& bytesJs, const trichi::Params& params) {
  const size_t floatsPerVertex = 6;
  const size_t vertexStride = floatsPerVertex * sizeof(float);
  std::vector<float> vertices{};
  std::vector<uint32_t> indices{};
  {
    const auto bytes = emscripten::convertJSArrayToNumberVector<uint8_t>(bytesJs);

    Assimp::Importer importer;
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 80.0);
    const struct aiScene* scene = importer.ReadFileFromMemory(
        bytes.data(),
        bytes.size(),
        aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_ImproveCacheLocality |
            aiProcess_OptimizeGraph |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType,
        fileName.c_str());

    for (int i = 0; i < scene->mMeshes[0]->mNumVertices; ++i) {
      vertices.push_back(scene->mMeshes[0]->mVertices[i].x);
      vertices.push_back(scene->mMeshes[0]->mVertices[i].y);
      vertices.push_back(scene->mMeshes[0]->mVertices[i].z);
      vertices.push_back(scene->mMeshes[0]->mNormals[i].x);
      vertices.push_back(scene->mMeshes[0]->mNormals[i].y);
      vertices.push_back(scene->mMeshes[0]->mNormals[i].z);
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

  auto hierarchy = convertToJsObject(trichi::buildClusterHierarchy(indices, vertices, vertexStride, params), true);

  std::cout << "Generated triangle cluster hierarchy\n";

  auto indicesJs = emscripten::val::global("Uint32Array").new_(indices.size());
  for (size_t i = 0; i < indices.size(); ++i) {
    indicesJs.set(i, indices[i]);
  }

  const auto quantizedVertices = toQuantizedVertexBytes(vertices);
  auto verticesJs = emscripten::val::global("Uint8Array").new_(quantizedVertices.size());
  for (size_t i = 0; i < quantizedVertices.size(); ++i) {
    verticesJs.set(i, quantizedVertices[i]);
  }

  float aabbMin[3] = {vertices[0], vertices[1], vertices[2]};
  float aabbMax[3] = {vertices[0], vertices[1], vertices[2]};
  for (size_t i = 6; i < vertices.size(); i += 6) {
    for (size_t c = 0; c < 3; ++c) {
      aabbMin[c] = std::min(aabbMin[c], vertices[i + c]);
      aabbMax[c] = std::max(aabbMax[c], vertices[i + c]);
    }
  }
  auto aabbMinJs = emscripten::val::global("Float32Array").new_(3);
  auto aabbMaxJs = emscripten::val::global("Float32Array").new_(3);
  for (size_t c = 0; c < 3; ++c) {
    aabbMinJs.set(c, aabbMin[c]);
    aabbMaxJs.set(c, aabbMax[c]);
  }

  hierarchy.set("indices", indicesJs);
  hierarchy.set("vertices", verticesJs);
  hierarchy.set("aabbMin", aabbMinJs);
  hierarchy.set("aabbMax", aabbMaxJs);
  hierarchy.set("vertexStrideFloats", 2);

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
