/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

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
  argparse::ArgumentParser program("per_meshlet_nuances");
  program.add_description("Create a meshlet hierarchy.");

  program.add_argument("-f", "--files")
      .help("a list of .OBJ files")
      .append()
      .nargs(argparse::nargs_pattern::at_least_one)
      .default_value(std::vector<std::string>{});

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& err) {
    printf("%s\n", program.help().str().c_str());
    return 1;
  }

  auto files = program.get<std::vector<std::string>>("--files");
  for (const auto& f : files) {
    const size_t floats_per_vertex = 3;  //(3 * 4);
    const size_t vertex_stride = floats_per_vertex * sizeof(float);
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

    trichi::TriChiParams params{};
    params.thread_pool_size = std::thread::hardware_concurrency();
    params.cluster_cone_weight = 0.0;
    const auto dag = trichi::build_cluster_hierarchy(indices, vertices, vertex_stride, params);


    float aabb_min[3] = {
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float aabb_max[3] = {
        std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), std::numeric_limits<float>::min()};

    std::ofstream js_stream("/home/lukas/Projects/trichi/demo/demo-mesh.js");
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
    for (size_t i = 0; i < dag.clusters.size(); ++i) {
      js_stream << dag.clusters[i].vertex_offset << ",";
      js_stream << dag.clusters[i].triangle_offset << ",";
      js_stream << dag.clusters[i].vertex_count << ",";
      js_stream << dag.clusters[i].triangle_count;
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

    js_stream << "  lods: new Uint32Array([";
    for (size_t i = 0; i < dag.lod_offsets.size(); ++i) {
      js_stream << dag.lod_offsets[i];
      if (i != dag.lod_offsets.size() - 1) {
        js_stream << ",";
      }
    }
    js_stream << "]),\n";

    js_stream << "  numMeshlets: " << dag.clusters.size() << ",\n";

    js_stream << "  maxClusterTriangles: " << params.max_triangles_per_cluster << ",\n";

    js_stream << "  aabb: {min: new Float32Array([" << aabb_min[0] << "," << aabb_min[1] << "," << aabb_min[2]
              << "]), max: new Float32Array([" << aabb_max[0] << "," << aabb_max[1] << "," << aabb_max[2] << "])},\n";

    js_stream << "  errors: new Float32Array([";
    for (size_t i = 0; i < dag.errors.size(); ++i) {
      const auto& node = dag.errors[i];
      js_stream << node.parent_error.center[0] << "," << node.parent_error.center[1] << "," << node.parent_error.center[2] << ",";
      js_stream << node.parent_error.radius << ",";
      js_stream << node.parent_error.error << ",";
      js_stream << node.cluster_error.center[0] << "," << node.cluster_error.center[1] << "," << node.cluster_error.center[2] << ",";
      js_stream << node.cluster_error.radius << ",";
      js_stream << node.cluster_error.error;
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
