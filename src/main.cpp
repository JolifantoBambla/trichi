#include <iostream>
#include <string>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "argparse/argparse.hpp"

#include "per_meshlet_nuances.hpp"

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
        /*
    vertices.push_back(scene->mMeshes[0]->mNormals[i].x);
    vertices.push_back(scene->mMeshes[0]->mNormals[i].y);
    vertices.push_back(scene->mMeshes[0]->mNormals[i].z);
    vertices.push_back(scene->mMeshes[0]->mTangents[i].x);
    vertices.push_back(scene->mMeshes[0]->mTangents[i].y);
    vertices.push_back(scene->mMeshes[0]->mTangents[i].z);
    vertices.push_back(scene->mMeshes[0]->mBitangents[i].x);
    vertices.push_back(scene->mMeshes[0]->mBitangents[i].y);
    vertices.push_back(scene->mMeshes[0]->mBitangents[i].z);
     */
      }
      for (int i = 0; i < scene->mMeshes[0]->mNumFaces; ++i) {
        if (scene->mMeshes[0]->mFaces[i].mNumIndices != 3) {
          throw std::runtime_error("encountered non-triangle face");
        }
        for (int j = 0; j < scene->mMeshes[0]->mFaces[i].mNumIndices; ++j) {
          indices.push_back(scene->mMeshes[0]->mFaces[i].mIndices[j]);
        }
      }

      printf(
          "mesh (%i / %i) has %i faces (%i indices = %i expected) and %i vertices (%i floats = %i expected)\n",
          0 + 1,
          int(scene->mNumMeshes),
          int(scene->mMeshes[0]->mNumFaces),
          int(indices.size()),
          int(scene->mMeshes[0]->mNumFaces * 3),
          int(scene->mMeshes[0]->mNumVertices),
          int(vertices.size()),
          int(scene->mMeshes[0]->mNumVertices * floats_per_vertex));
    }

    pmn::create_dag(indices, vertices, vertex_stride);
  }

  return 0;
}
