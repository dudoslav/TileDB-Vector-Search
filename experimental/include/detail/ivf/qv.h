/**
 * @file   ivf/qv.h
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
 *
 */

#ifndef TILEDB_IVF_QV_H
#define TILEDB_IVF_QV_H

#include <map>

#include "algorithm.h"
#include "flat_query.h"
#include "linalg.h"
#include "tdb_matrix.h"
#include "tdb_partitioned_matrix.h"

extern double global_time_of_interest;

namespace detail::ivf {

/**
 * @brief Query a (small) set of query vectors against a vector database.
 * This version loads the entire partition array into memory and then
 * queries each vector in the query set against the appropriate partitions.
 */
auto qv_query_heap_infinite_ram(
    tiledb::Context& ctx,
    const std::string& part_uri,
    auto&& centroids,
    auto&& q,
    auto&& indices,
    const std::string& id_uri,
    size_t nprobe,
    size_t k_nn,
    bool nth,
    size_t nthreads) {
  life_timer _{"Total time " + tdb_func__};


  // Read the shuffled database and ids
  // @todo To this more systematically
  auto shuffled_db = tdbColMajorMatrix<shuffled_db_type>(ctx, part_uri);
  auto shuffled_ids = read_vector<shuffled_ids_type>(ctx, id_uri);

  assert(shuffled_db.num_cols() == shuffled_ids.size());
  if (size(indices) == centroids.num_cols()) {
    indices.resize(size(indices) + 1);
    indices[size(indices) - 1] = shuffled_db.num_cols();
  }
  assert(size(indices) == centroids.num_cols() + 1);

  debug_matrix(shuffled_db, "shuffled_db");
  debug_matrix(shuffled_ids, "shuffled_ids");

  // get closest centroid for each query vector
  // auto top_k = qv_query(centroids, q, nprobe, nthreads);
  //  auto top_centroids = vq_query_heap(centroids, q, nprobe, nthreads);

  // @todo is this the best (fastest) algorithm to use?  (it takes miniscule
  // time)
  auto top_centroids =
      detail::flat::qv_query_nth(centroids, q, nprobe, false, nthreads);

  auto min_scores = std::vector<fixed_min_pair_heap<float, size_t>>(
      size(q), fixed_min_pair_heap<float, size_t>(k_nn));
  // auto min_scores = std::vector<fixed_min_heap<std::pair<float,
  // size_t>>>(size(q), fixed_min_heap<std::pair<float, size_t>>(k_nn));

  life_timer __{std::string{"In memory portion of "} + tdb_func__};
  auto par = stdx::execution::indexed_parallel_policy{nthreads};
  stdx::range_for_each(
      std::move(par), q, [&, nprobe](auto&& q_vec, auto&& n = 0, auto&& j = 0) {
        for (size_t p = 0; p < nprobe; ++p) {
          size_t start = indices[top_centroids(p, j)];
          size_t stop = indices[top_centroids(p, j) + 1];

          for (size_t i = start; i < stop; ++i) {
            auto score = L2(q_vec /*q[j]*/, shuffled_db[i]);
            min_scores[j].insert(score, shuffled_ids[i]);
            // min_scores[j].insert({score, shuffled_ids[i]});
          }
        }
      });

  ColMajorMatrix<size_t> top_k(k_nn, q.num_cols());

  life_timer ___{std::string{"Top k portion of "} + tdb_func__};

  // get_top_k_from_heap(min_scores, top_k);

  // @todo get_top_k_from_heap
  for (int j = 0; j < size(q); ++j) {
    sort_heap(min_scores[j].begin(), min_scores[j].end());
    std::transform(
        min_scores[j].begin(),
        min_scores[j].end(),
        top_k[j].begin(),
        ([](auto&& e) { return std::get<1>(e); }));
  }

  // @todo this is an ugly and embarrassing hack
  _.stop();
  global_time_of_interest = _.elapsed();

  return top_k;
}

auto qv_query_heap_finite_ram(
    tiledb::Context& ctx,
    const std::string& part_uri,
    auto&& centroids,
    auto&& q,
    auto&& indices,
    const std::string& id_uri,
    size_t nprobe,
    size_t k_nn,
    size_t upper_bound,
    bool nth,
    size_t nthreads) {
  life_timer _{"Total time " + tdb_func__};

  size_t num_queries = size(q);

  // get closest centroid for each query vector
  auto top_centroids =
      detail::flat::qv_query_nth(centroids, q, nprobe, false, nthreads);

  /*
   * `top_centroids` maps from rank X query index to the centroid index.
   *
   * To process centroids (partitions) in order, we need to map from `centroid`
   * to the set of queries having that centroid.
   *
   * We also need to know the "active" centroids, i.e., the ones having at
   * least one query.
   */
  auto centroid_query = std::multimap<parts_type, size_t>{};
  auto active_centroids = std::set<parts_type>{};
  for (size_t j = 0; j < num_queries; ++j) {
    for (size_t p = 0; p < nprobe; ++p) {
      auto tmp = top_centroids(p, j);
      centroid_query.emplace(top_centroids(p, j), j);
      active_centroids.emplace(top_centroids(p, j));
    }
  }

  // debug_slice(top_centroids, "top_centroids",
  // std::min<size_t>(16, top_centroids.num_rows()),
  // std::min<size_t>(16, top_centroids.num_cols()));

  auto active_partitions =
      std::vector<parts_type>(begin(active_centroids), end(active_centroids));
  // std::copy(begin(active_centroids), end(active_centroids),
  // begin(active_partitions));

  /*
   * Read the necessary partitions and ids
   */
  if (size(indices) != centroids.num_cols() + 1) {
    std::cout << "#\n# indices " << size(indices)
              << " != " << centroids.num_cols() + 1 << std::endl;
    std::cout << "# some minimal inaccuracy until fixed\n#" << std::endl;
    indices.resize(centroids.num_cols() + 1);
    indices[size(indices) - 1] = indices[size(indices) - 2];
  }
  std::vector<parts_type> new_indices(size(active_partitions) + 1);
  new_indices[0] = 0;
  for (size_t i = 0; i < size(active_partitions); ++i) {
    new_indices[i + 1] = new_indices[i] + indices[active_partitions[i] + 1] -
                         indices[active_partitions[i]];
  }

  // std::vector<shuffled_ids_type> shuffled_ids;

  auto shuffled_db = tdbColMajorPartitionedMatrix<shuffled_db_type>(
      part_uri,
      std::move(indices),
      active_partitions,
      id_uri,
      upper_bound,
      /* shuffled_ids,*/ nthreads);

  assert(shuffled_db.num_cols() == size(shuffled_db.ids()));

  debug_matrix(shuffled_db, "shuffled_db");
  debug_matrix(shuffled_db.ids(), "shuffled_db.ids()");


  // auto min_scores = std::vector<fixed_min_pair_heap<float, size_t>>(
  //       size(q), fixed_min_pair_heap<float, size_t>(k_nn));

  std::vector<std::vector<fixed_min_pair_heap<float, size_t>>> min_scores(
      nthreads,
      std::vector<fixed_min_pair_heap<float, size_t>>(
          size(q), fixed_min_pair_heap<float, size_t>(k_nn)));

  life_timer __{std::string{"Iteration portion of "} + tdb_func__};

  for (;;) {
    // size_t block_size = (size(active_partitions) + nthreads - 1) / nthreads;
    size_t parts_per_thread =
        (shuffled_db.num_col_parts() + nthreads - 1) / nthreads;

    std::vector<std::future<void>> futs;
    futs.reserve(nthreads);

    for (size_t n = 0; n < nthreads; ++n) {
      auto first_part =
          std::min<size_t>(n * parts_per_thread, shuffled_db.num_col_parts());
      auto last_part = std::min<size_t>(
          (n + 1) * parts_per_thread, shuffled_db.num_col_parts());

      if (first_part != last_part) {
        futs.emplace_back(
            std::async(std::launch::async, [&, n, first_part, last_part]() {
              /*
               * For each partition, process the queries that have that
               * partition as their top centroid.
               */
              for (size_t p = first_part; p < last_part; ++p) {
                auto partno = p + shuffled_db.col_part_offset();
                auto start = new_indices[partno];
                auto stop = new_indices[partno + 1];

                /*
                 * Get the queries associated with this partition.
                 */
                auto range =
                    centroid_query.equal_range(active_partitions[partno]);
                for (auto i = range.first; i != range.second; ++i) {
                  auto j = i->second;
                  auto q_vec = q[j];

                  // @todo shift start / stop back by the offset
                  for (size_t k = start; k < stop; ++k) {
                    auto kp = k - shuffled_db.col_offset();

                    auto score = L2(q_vec, shuffled_db[kp]);

                    // @todo any performance with apparent extra indirection?
                    min_scores[n][j].insert(score, shuffled_db.ids()[kp]);
                  }
                }
              }
            }));
      }
    }

    for (int n = 0; n < size(futs); ++n) {
      futs[n].get();
    }

    if (!shuffled_db.advance()) {
      break;
    }
  }

  for (int j = 0; j < size(q); ++j) {
    for (int n = 1; n < nthreads; ++n) {
      for (auto&& e : min_scores[n][j]) {
        min_scores[0][j].insert(std::get<0>(e), std::get<1>(e));
      }
    }
  }

  ColMajorMatrix<size_t> top_k(k_nn, q.num_cols());

  life_timer ___{std::string{"Top k portion of "} + tdb_func__};

  // get_top_k_from_heap(min_scores, top_k);

  // @todo get_top_k_from_heap
  for (int j = 0; j < size(q); ++j) {
    sort_heap(min_scores[0][j].begin(), min_scores[0][j].end());
    std::transform(
        min_scores[0][j].begin(),
        min_scores[0][j].end(),
        top_k[j].begin(),
        ([](auto&& e) { return std::get<1>(e); }));
  }

  // @todo this is an ugly and embarrassing hack
  _.stop();
  global_time_of_interest = _.elapsed();

  return top_k;
}

#if 0
auto kmeans_query_small_q_minparts(
    const std::string& part_uri,
    auto&& centroids,
    auto&& q,
    auto&& indices,
    const std::string& id_uri,
    size_t nprobe,
    size_t k_nn,
    bool nth,
    size_t nthreads) {

  size_t num_queries = q.num_cols();
  // get closest centroid for each query vector
  // auto top_k = qv_query(centroids, q, nprobe, nthreads);
  //  auto top_centroids = vq_query_heap(centroids, q, nprobe, nthreads);
  auto top_centroids = qv_query_nth(centroids, q, nprobe, false, nthreads);

  std::vector<std::multiset<size_t>> queries_per_centroid(top_centroids.num_cols(), std::multiset<size_t>{});
  for (size_t j = 0; j < num_queries; ++j) {
    for (size_t p = 0; p < nprobe; ++p) {
      queries_per_centroid[j].insert(top_centroids(p, j));
    }
  }
  std::sort(begin(raveled(top_centroids)), end(raveled(top_centroids)));
  auto new_begin = begin(raveled(top_centroids));
  auto new_end = std::unique(begin(raveled(top_centroids)), end(raveled(top_centroids)));
  std::vector<parts_type> parts(new_begin, new_end);

  std::map<size_t, size_t> part_map;
  for (size_t i = 0; i < parts.size(); ++i) {
    part_map[parts[i]] = i;
  }

  // Read the shuffled database and ids
  // @todo To this more systematically
  // auto shuffled_db = tdbColMajorMatrix<shuffled_db_type>(part_uri);
  // auto shuffled_ids = read_vector<shuffled_ids_type>(id_uri);

  std::vector<shuffled_ids_type> shuffled_ids;
  auto shuffled_db = tdbColMajorMatrix<shuffled_db_type>(part_uri, indices, parts, id_uri, shuffled_ids, nthreads);

  debug_matrix(shuffled_db, "shuffled_db");
  debug_matrix(shuffled_ids, "shuffled_ids");

  auto min_scores = std::vector<fixed_min_heap<element>>(
      size(q), fixed_min_heap<element>(k_nn));

  life_timer __{std::string{"In memory portion of "} + tdb_func__};
  auto par = stdx::execution::indexed_parallel_policy{nthreads};

  stdx::range_for_each(
      std::move(par), q, [&, nprobe](auto&& q_vec, auto&& n = 0, auto&& j = 0) {
        for (size_t p = 0; p < nprobe; ++p) {
          size_t start = part_map[indices[top_centroids(p, j)]];
          size_t stop = part_map[indices[top_centroids(p, j) + 1]];

          for (size_t i = start; i < stop; ++i) {
            auto score = L2(q[j], shuffled_db[i]);
            min_scores[j].insert(element{score, shuffled_ids[i]});
          }
        }
      });


  ColMajorMatrix<size_t> top_k(k_nn, q.num_cols());


  life_timer ___{std::string{"Top k portion of "} + tdb_func__};

  // @todo this pattern repeats alot -- put into a function
  for (int j = 0; j < size(q); ++j) {
    sort_heap(min_scores[j].begin(), min_scores[j].end());
    std::transform(
        min_scores[j].begin(),
        min_scores[j].end(),
        top_k[j].begin(),
        ([](auto&& e) { return e.second; }));
  }

  // @todo this is an ugly and embarrassing hack
  __.stop();
  global_time_of_interest = __.elapsed();

  return top_k;
}
#endif

}  // namespace detail::ivf

#endif  // TILEDB_IVF_QV_H