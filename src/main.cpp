/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "argparse/argparse.hpp"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "trichi.hpp"

int main(int argc, char* argv[]) {
  argparse::ArgumentParser program("example");
  program.add_description("Creates clusters hierarchies and dump them as JS files.");

  program.add_argument("-f", "--files")
      .help("a list of model files (e.g., OBJ files)")
      .append()
      .nargs(argparse::nargs_pattern::at_least_one)
      .default_value(std::vector<std::string>{});

  program.add_argument("-o", "--outputdir")
    .help("the output directory")
    .append()
    .nargs(argparse::nargs_pattern::at_least_one)
    .default_value(std::vector<std::string>{});

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& err) {
    printf("%s\n", program.help().str().c_str());
    return 1;
  }

  const std::filesystem::path output_dir = program.get<std::string>("-o");
  for (auto files = program.get<std::vector<std::string>>("--files"); const auto& f : files) {
    constexpr size_t vertexStride = 3 * sizeof(float);
    std::vector<float> vertices{};
    std::vector<uint32_t> indices{};
    {
      Assimp::Importer importer;
      const struct aiScene* scene = importer.ReadFile(
          f, aiProcess_Triangulate | aiProcess_OptimizeGraph | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);

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

    trichi::Params params{};
    params.threadPoolSize = std::thread::hardware_concurrency();
    params.clusterConeWeight = 0.0;
    const auto dag = trichi::buildClusterHierarchy(indices, vertices, vertexStride, params);

    float aabbMin[3] = {
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float aabbMax[3] = {
        std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), std::numeric_limits<float>::min()};

    std::ofstream js_stream(output_dir / (f + ".js"));
    js_stream << "export const mesh = {\n";
    js_stream << "  vertices: new Float32Array([";
    for (size_t i = 0; i < vertices.size(); ++i) {
      js_stream << vertices[i];
      if (i != vertices.size() - 1) {
        js_stream << ",";
      }
      if (i % 3 == 0) {
        for (size_t c = 0; c < 3; ++c) {
          aabbMin[c] = std::min(aabbMin[c], vertices[i + c]);
          aabbMax[c] = std::max(aabbMax[c], vertices[i + c]);
        }
      }
    }
    js_stream << "]),\n";

    js_stream << "  strideFloats: " << vertexStride / sizeof(float) << ",\n";

    js_stream << "  clusters: new Uint32Array([";
    for (size_t i = 0; i < dag.clusters.size(); ++i) {
      js_stream << dag.clusters[i].vertexOffset << ",";
      js_stream << dag.clusters[i].triangleOffset << ",";
      js_stream << dag.clusters[i].vertexCount << ",";
      js_stream << dag.clusters[i].triangleCount;
      if (i != dag.clusters.size() - 1) {
        js_stream << ",";
      }
    }
    js_stream << "]),\n";

    js_stream << "  meshletVertices: new Uint32Array([";
    for (size_t i = 0; i < dag.vertices.size(); ++i) {
      js_stream << dag.vertices[i];
      if (i != dag.vertices.size() - 1) {
        js_stream << ",";
      }
    }
    js_stream << "]),\n";

    js_stream << "  meshletTriangles: new Uint32Array([";
    for (size_t i = 0; i < dag.triangles.size(); ++i) {
      js_stream << static_cast<uint32_t>(dag.triangles[i]);
      if (i != dag.triangles.size() - 1) {
        js_stream << ",";
      }
    }
    js_stream << "]),\n";

    js_stream << "  numMeshlets: " << dag.clusters.size() << ",\n";

    js_stream << "  maxClusterTriangles: " << params.maxTrianglesPerCluster << ",\n";

    js_stream << "  aabb: {min: new Float32Array([" << aabbMin[0] << "," << aabbMin[1] << "," << aabbMin[2]
              << "]), max: new Float32Array([" << aabbMax[0] << "," << aabbMax[1] << "," << aabbMax[2] << "])},\n";

    js_stream << "  errors: new Float32Array([";
    for (size_t i = 0; i < dag.errors.size(); ++i) {
      const auto& node = dag.errors[i];
      js_stream << node.parentError.center[0] << "," << node.parentError.center[1] << "," << node.parentError.center[2] << ",";
      js_stream << node.parentError.radius << ",";
      js_stream << node.parentError.error << ",";
      js_stream << node.clusterError.center[0] << "," << node.clusterError.center[1] << "," << node.clusterError.center[2] << ",";
      js_stream << node.clusterError.radius << ",";
      js_stream << node.clusterError.error;
      if (i < dag.errors.size() - 1) {
        js_stream << ",";
      }
    }
    js_stream << "]),\n";

    js_stream << "  bounds: new Float32Array([";
    for (size_t i = 0; i < dag.bounds.size(); ++i) {
      const auto& node = dag.bounds[i];
      js_stream << node.center[0] << "," << node.center[1] << "," << node.center[2] << ",";
      js_stream << node.radius;/* << ",";
      js_stream << node.normal_cone.apex[0] << "," << node.normal_cone.apex[1] << "," << node.normal_cone.apex[2] << ",";
      js_stream << node.normal_cone.axis[0] << "," << node.normal_cone.axis[1] << "," << node.normal_cone.axis[2] << ",";
      js_stream << node.normal_cone.cutoff;
      */
      if (i < dag.errors.size() - 1) {
        js_stream << ",";
      }
    }
    js_stream << "]),\n";

    js_stream << "}" << std::endl;
  }

  return 0;
}
