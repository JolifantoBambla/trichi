/**
* Copyright (c) 2024 Lukas Herzberger
* SPDX-License-Identifier: MIT
*/

#ifndef TRICHI_UTIL_HPP
#define TRICHI_UTIL_HPP

#include <algorithm>          // std::set_intersection
#include <cstdint>            // uint64_t

#ifdef TRICHI_PARALLEL
#include "BS_thread_pool.hpp" // thread_pool
#endif //TRICHI_PARALLEL

namespace trichi {
[[nodiscard]] constexpr uint64_t pack_sorted(uint32_t a, uint32_t b) {
  return (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
}

class LoopRunner {
 public:
  explicit LoopRunner(size_t thread_count)
#ifdef TRICHI_PARALLEL
      : thread_pool(thread_count)
#endif //TRICHI_PARALLEL
  {}

  template<typename Body>
  void loop(size_t start, size_t end, Body&& body) {
#ifdef TRICHI_PARALLEL
    thread_pool.detach_loop<size_t>(start, end, body);
    thread_pool.wait();
#else
    for (size_t i = start; i < end; ++i) {
      body(i);
    }
#endif //TRICHI_PARALLEL
  }
 private:
#ifdef TRICHI_PARALLEL
  BS::thread_pool thread_pool;
#endif //TRICHI_PARALLEL
};

// https://stackoverflow.com/questions/32640327/how-to-compute-the-size-of-an-intersection-of-two-stl-sets-in-c
struct Counter {
  struct value_type {
    template <typename T>
    value_type(const T&) {}
  };

  void push_back(const value_type&) { ++count; }

  size_t count = 0;
};

template <typename T1, typename T2>
size_t intersection_size(const T1& s1, const T2& s2) {
  Counter c{};
  std::set_intersection(s1.begin(), s1.end(), s2.begin(), s2.end(), std::back_inserter(c));
  return c.count;
}
}  // namespace trichi

#endif  //TRICHI_UTIL_HPP
