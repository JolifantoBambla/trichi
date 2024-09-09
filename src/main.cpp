/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

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
    const auto dag = trichi::build_cluster_hierarchy(indices, vertices, vertex_stride, params);

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
  }

  return 0;
}
