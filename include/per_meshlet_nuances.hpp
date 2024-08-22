//
// Created by lukas on 21.08.24.
//

#ifndef METIS_PER_MESHLET_NUANCES_HPP
#define METIS_PER_MESHLET_NUANCES_HPP

#include <iostream> // todo: remove
#include <set>
#include <unordered_map>
#include <vector>

#include "meshoptimizer.h"
#include "metis.h"

// https://stackoverflow.com/questions/32640327/how-to-compute-the-size-of-an-intersection-of-two-stl-sets-in-c
struct Counter {
  struct value_type { template<typename T> value_type(const T&) { } };
  void push_back(const value_type&) { ++count; }
  size_t count = 0;
};
template<typename T1, typename T2>
size_t intersection_size(const T1& s1, const T2& s2) {
  Counter c{};
  set_intersection(s1.begin(), s1.end(), s2.begin(), s2.end(), std::back_inserter(c));
  return c.count;
}
template<typename T1, typename T2>
size_t union_size(const T1& s1, const T2& s2) {
  Counter c{};
  set_union(s1.begin(), s1.end(), s2.begin(), s2.end(), std::back_inserter(c));
  return c.count;
}

namespace pmn {
    // todo: figure out API, move impl to private files
    void generateNextLod() {

    }

    void create_dag(const std::vector<uint32_t> &indices, const std::vector<float> &vertices, size_t vertex_stride) {
        // todo: should be params
        const size_t max_vertices = 255;
        const size_t max_triangles = 128;
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
          // find edges
          std::unordered_map<uint64_t, int> edges{};
          for (size_t i = 0; i < meshlet.triangle_count; ++i) {
            const uint64_t a = meshlet_triangles[meshlet.triangle_offset + i * 3 + 0];
            const uint64_t b = meshlet_triangles[meshlet.triangle_offset + i * 3 + 1];
            const uint64_t c = meshlet_triangles[meshlet.triangle_offset + i * 3 + 2];
            const uint64_t e1 = a < b ? (a << 32) | b : (b << 32) | a;
            const uint64_t e2 = a < c ? (a << 32) | c : (c << 32) | a;
            const uint64_t e3 = b < c ? (b << 32) | c : (c << 32) | b;
            if (!edges.contains(e1)) {
              edges[e1] = 1;
            } else {
              ++edges[e1];
            }
            if (!edges.contains(e2)) {
              edges[e2] = 1;
            } else {
              ++edges[e2];
            }
            if (!edges.contains(e3)) {
              edges[e3] = 1;
            } else {
              ++edges[e3];
            }
          }

          // find boundary = find edges that only appear once
          boundaries.emplace_back();
          auto& boundary = boundaries.back();
          for (const auto& [k, v] : edges) {
            if (v == 1) {
              boundary.insert(k);
            }
          }

          printf("boundary size: %i vs %i edges\n", int(boundary.size()), int(edges.size()));
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
