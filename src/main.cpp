#include <iostream>
#include <string>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "per_meshlet_nuances.hpp"

int main() {
    const std::string meshPath = "/home/lherzberger/Projects/per-meshlet-nuances/assets/stanford_bunny.obj";//"/home/lukas/Projects/per-meshlet-nuances/assets/stanford_bunny.obj";

    Assimp::Importer importer;
    const struct aiScene* scene = importer.ReadFile(meshPath,
                      aiProcess_CalcTangentSpace |
                      aiProcess_Triangulate |
                      aiProcess_JoinIdenticalVertices |
                      aiProcess_SortByPType);

    const size_t vertex_stride = (3 * 4) * 4;
    std::vector<float> vertices{};
    std::vector<uint32_t> indices{};
    for (int i = 0; i < scene->mMeshes[0]->mNumVertices; ++i) {
        vertices.push_back(scene->mMeshes[0]->mVertices[i].x);
        vertices.push_back(scene->mMeshes[0]->mVertices[i].y);
        vertices.push_back(scene->mMeshes[0]->mVertices[i].z);
        vertices.push_back(scene->mMeshes[0]->mNormals[i].x);
        vertices.push_back(scene->mMeshes[0]->mNormals[i].y);
        vertices.push_back(scene->mMeshes[0]->mNormals[i].z);
        vertices.push_back(scene->mMeshes[0]->mTangents[i].x);
        vertices.push_back(scene->mMeshes[0]->mTangents[i].y);
        vertices.push_back(scene->mMeshes[0]->mTangents[i].z);
        vertices.push_back(scene->mMeshes[0]->mBitangents[i].x);
        vertices.push_back(scene->mMeshes[0]->mBitangents[i].y);
        vertices.push_back(scene->mMeshes[0]->mBitangents[i].z);
    }
    for (int i = 0; i < scene->mMeshes[0]->mNumFaces; ++i) {
        for (int j = 0; j < scene->mMeshes[0]->mFaces[i].mNumIndices; ++j) {
            indices.push_back(scene->mMeshes[0]->mFaces[i].mIndices[j]);
        }
    }

    pmn::create_dag(indices, vertices, vertex_stride);

    std::cout << vertices.size() << " " << indices.size() << std::endl;

    return 0;
}
