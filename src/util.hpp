//
// Created by lukas on 28.08.24.
//

#ifndef PER_MESHLET_NUANCES_UTIL_HPP
#define PER_MESHLET_NUANCES_UTIL_HPP

#include <algorithm>

namespace pmn {
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

[[nodiscard]] constexpr uint64_t pack_sorted(uint32_t a, uint32_t b) {
  return (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
}
}  // namespace pmn

#endif  //PER_MESHLET_NUANCES_UTIL_HPP
