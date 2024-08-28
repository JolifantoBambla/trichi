//
// Created by lukas on 28.08.24.
//

#include "impl.hpp"

namespace pmn {
#ifdef PMN_PARALLEL
[[nodiscard]] std::vector<std::vector<uint64_t>> extract_boundaries(
    const std::vector<Cluster>& clusters,
    BS::thread_pool& threadPool) {
  std::vector<std::vector<uint64_t>> boundaries(clusters.size());
  threadPool.detach_loop<size_t>(
      0, clusters.size(), [&clusters, &boundaries](const size_t i) { extractBoundary(clusters[i], boundaries[i]); });
  threadPool.wait();
  return std::move(boundaries);
}
#else
[[nodiscard]] std::vector<std::vector<uint64_t>> extract_boundaries(const std::vector<Cluster>& clusters) {
  std::vector<std::vector<uint64_t>> boundaries(clusters.size());
  for (size_t i = 0; i < clusters.size(); ++i) {
    extract_boundary(clusters[i], boundaries[i]);
  }
  return std::move(boundaries);
}
#endif
}  // namespace pmn