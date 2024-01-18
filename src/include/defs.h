/**
 * @file   defs.h
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2023 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * Support functions for TiledB vector search algorithms.
 *
 */
#if 0
#ifndef TDB_DEFS_H
#define TDB_DEFS_H

#if 0
template <class L, class I>
auto verify_top_k_index(L const& top_k, I const& g, int k, int qno) {
  // std::sort(begin(g), begin(g) + k);
  // std::sort(begin(top_k), end(top_k));

  if (!std::equal(begin(top_k), begin(top_k) + k, g.begin())) {
    std::cout << "Query " << qno << " is incorrect" << std::endl;
    for (int i = 0; i < std::min<int>(10, k); ++i) {
      std::cout << "(" << top_k[i] << " != " << g[i] << ")  ";
    }
    std::cout << std::endl;
    std::cout << std::endl;
  }
}

/**
 * @brief Check the computed top k vectors against the ground truth.
 * Useful only for exact search.
 * Prints diagnostic message if difference is found.
 * @todo Handle the error more systematically and succinctly.
 */
template <class V, class L, class I>
auto verify_top_k(V const& scores, L const& top_k, I const& g, int k, int qno) {
  if (!std::equal(
          begin(top_k), begin(top_k) + k, g.begin(), [&](auto& a, auto& b) {
            return scores[a] == scores[b];
          })) {
    std::cout << "Query " << qno << " is incorrect" << std::endl;
    for (int i = 0; i < std::min<int>(10, k); ++i) {
      std::cout << "  (" << top_k[i] << " " << scores[top_k[i]] << ") ";
    }
    std::cout << std::endl;
    for (int i = 0; i < std::min(10, k); ++i) {
      std::cout << "  (" << g[i] << " " << scores[g[i]] << ") ";
    }
    std::cout << std::endl;
    std::cout << std::endl;
  }
}

/**
 * @brief Check the computed top k vectors against the ground truth.
 * Useful only for exact search.
 */
template <class L, class I>
auto verify_top_k(L const& top_k, I const& g, int k, int qno) {
  if (!std::equal(begin(top_k), begin(top_k) + k, g.begin())) {
    std::cout << "Query " << qno << " is incorrect" << std::endl;
    for (int i = 0; i < std::min(k, 10); ++i) {
      std::cout << "  (" << top_k[i] << " " << g[i] << ")";
    }
    std::cout << std::endl;
  }
}

// @todo implement with fixed_min_heap
template <class V, class L, class I>
auto get_top_k_nth(V const& scores, L&& top_k, I& index, int k) {
  std::iota(begin(index), end(index), 0);
  std::nth_element(
      begin(index), begin(index) + k, end(index), [&](auto&& a, auto&& b) {
        return scores[a] < scores[b];
      });
  std::copy(begin(index), begin(index) + k, begin(top_k));
  std::sort(begin(top_k), end(top_k), [&](auto& a, auto& b) {
    return scores[a] < scores[b];
  });
  return top_k;
}

template <class V, class L>
auto get_top_k(V const& scores, L&& top_k, int k) {
  using element = std::pair<float, unsigned>;
  fixed_min_heap<element> s(k);

  auto num_scores = scores.size();
  for (size_t i = 0; i < num_scores; ++i) {
    s.insert({scores[i], i});
  }
  std::sort_heap(begin(s), end(s));
  std::transform(
      s.begin(), s.end(), top_k.begin(), ([](auto&& e) { return e.second; }));

  return top_k;
}

template <class S>
auto get_top_k(const S& scores, int k, bool nth, int nthreads) {
  scoped_timer _{"Get top k"};

  auto num_queries = scores.num_cols();

  auto top_k = ColMajorMatrix<size_t>(k, num_queries);

  int q_block_size = (num_queries + nthreads - 1) / nthreads;
  std::vector<std::future<void>> futs;
  futs.reserve(nthreads);

  for (int n = 0; n < nthreads; ++n) {
    int q_start = n * q_block_size;
    int q_stop = std::min<int>((n + 1) * q_block_size, num_queries);

    if (nth) {
      futs.emplace_back(std::async(
          std::launch::async, [q_start, q_stop, &scores, &top_k, k]() {
            std::vector<int> index(scores.num_rows());

            for (int j = q_start; j < q_stop; ++j) {
              get_top_k_nth(scores[j], std::move(top_k[j]), index, k);
            }
          }));
    } else {
      futs.emplace_back(std::async(
          std::launch::async, [q_start, q_stop, &scores, &top_k, k]() {
            std::vector<int> index(scores.num_rows());

            for (int j = q_start; j < q_stop; ++j) {
              get_top_k(scores[j], std::move(top_k[j]), k);
            }
          }));
    }
  }
  for (int n = 0; n < nthreads; ++n) {
    futs[n].get();
  }
  return top_k;
}

// @todo use get_top_k_from_heap
void get_top_k_from_heap(auto&& min_scores, auto&& top_k) {
  std::sort_heap(begin(min_scores), end(min_scores));
  std::transform(
      begin(min_scores), end(min_scores), begin(top_k), ([](auto&& e) {
        return std::get<1>(e);
      }));
}

template <class TK, class G>
bool validate_top_k(TK& top_k, G& g) {
  size_t k = top_k.num_rows();
  size_t num_errors = 0;

  for (size_t qno = 0; qno < top_k.num_cols(); ++qno) {
    // @todo -- count intersections rather than testing for equality
    std::sort(begin(top_k[qno]), end(top_k[qno]));
    std::sort(begin(g[qno]), begin(g[qno]) + top_k.num_rows());

    if (!std::equal(begin(top_k[qno]), begin(top_k[qno]) + k, begin(g[qno]))) {
      if (num_errors++ > 10) {
        return false;
      }
      std::cout << "Query " << qno << " is incorrect" << std::endl;
      for (size_t i = 0; i < std::min(k, static_cast<size_t>(10UL)); ++i) {
        std::cout << "  (" << top_k(i, qno) << " " << g(i, qno) << ")";
      }
      std::cout << std::endl;
    }
  }

  return true;
}
#endif  // 0
#endif  // TDB_DEFS_H
#endif  // 0
