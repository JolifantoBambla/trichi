//
// Created by lukas on 21.08.24.
//

#ifndef METIS_PER_MESHLET_NUANCES_HPP
#define METIS_PER_MESHLET_NUANCES_HPP

#include <iostream> // todo: remove
#include <set>
#include <vector>

#include "geometrycentral/surface/manifold_surface_mesh.h"
#include "geometrycentral/surface/surface_mesh.h"
#include "meshoptimizer.h"
#include "metis.h"

namespace pmn {
    // todo: figure out API, move impl to private files
    void generateNextLod() {

    }

    void create_dag(const std::vector<uint32_t> &indices, const std::vector<float> &vertices, size_t vertex_stride) {
        // todo: should be params
        const size_t max_vertices = 255;
        const size_t max_triangles = 64;
        const float cone_weight = 0.5f;

        // todo: optimize mesh before generating meshlets

        size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);
        std::vector<meshopt_Meshlet> meshlets(max_meshlets);
        std::vector<unsigned int> meshlet_vertices(max_meshlets * max_vertices);
        std::vector<unsigned char> meshlet_triangles(max_meshlets * max_triangles * 3);

        meshlets.resize(meshopt_buildMeshlets(
                meshlets.data(),
                meshlet_vertices.data(),
                meshlet_triangles.data(),
                indices.data(),
                indices.size(),
                vertices.data(),
                vertices.size(),
                vertex_stride,
                max_vertices,
                max_triangles,
                cone_weight));

        const meshopt_Meshlet& last = meshlets.back();
        meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
        meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));

        for (const auto& meshlet : meshlets) {
            meshopt_optimizeMeshlet(&meshlet_vertices[meshlet.vertex_offset],
                                    &meshlet_triangles[meshlet.triangle_offset],
                                    meshlet.triangle_count,
                                    meshlet.vertex_count);
        }

        double avg_vertices = 0;
        double avg_triangles = 0;
        size_t not_full = 0;

        for (auto m : meshlets) {
            avg_vertices += m.vertex_count;
            avg_triangles += m.triangle_count;
            not_full += m.vertex_count < max_vertices;
        }

        avg_vertices /= double(meshlets.size());
        avg_triangles /= double(meshlets.size());

        printf("Meshlets: %d meshlets (avg vertices %.1f, avg triangles %.1f, not full %d)\n",
               int(meshlets.size()), avg_vertices, avg_triangles, int(not_full));

        // todo: clean meshlets: move triangles with one or less shared vertices to other meshlet

        std::vector<meshopt_Bounds> bounds{};
        bounds.reserve(meshlets.size());
        for (const auto& meshlet : meshlets) {
            bounds.push_back(meshopt_computeMeshletBounds(&meshlet_vertices[meshlet.vertex_offset],
                                                          &meshlet_triangles[meshlet.triangle_offset],
                                                          meshlet.triangle_count,
                                                          vertices.data(),
                                                          vertices.size(),
                                                          vertex_stride));
        }

        std::vector<std::set<uint64_t>> boundaries{};
        boundaries.reserve(meshlets.size());
        for (const auto& meshlet : meshlets) {
          std::vector<std::vector<size_t>> polygons{};
          polygons.reserve(meshlet.triangle_count);
          for (size_t i = 0; i < meshlet.triangle_count; ++i) {
             polygons.push_back({
                 meshlet_triangles[meshlet.triangle_offset + i * 3 + 0],
                 meshlet_triangles[meshlet.triangle_offset + i * 3 + 1],
                 meshlet_triangles[meshlet.triangle_offset + i * 3 + 2],
             });
          }

          std::set<uint64_t> boundary{};
          for (const auto& e : geometrycentral::surface::SurfaceMesh(polygons).edges()) {
            if (e.isBoundary()) {
              printf("first %i second %i\n", e.firstVertex().getIndex(), e.secondVertex().getIndex());
              auto start = static_cast<uint64_t>(meshlet_vertices[meshlet.vertex_offset + e.firstVertex().getIndex()]);
              auto end = static_cast<uint64_t>(meshlet_vertices[meshlet.vertex_offset + e.secondVertex().getIndex()]);
              if (start > end) {
                std::swap(start, end);
              }
              boundary.insert((start << 32) | end);
            }
          }
          boundaries.push_back(boundary);
          printf("meshlet done\n");
        }

        // todo:
        // compute boundary for each cluster
        // group up to 4 clusters based on number of shared boundary edges
        // merge clusters in group (deduplicate)
        // simplify to 50% of tris in group
        // split groups into 2 clusters of n triangles each
        // repeat
    }
}

#endif //METIS_PER_MESHLET_NUANCES_HPP
