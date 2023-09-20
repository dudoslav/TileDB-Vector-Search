/**
 * @file   unit_ivf_vq.cc
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
 */

#include <catch2/catch_all.hpp>
#include "detail/ivf/qv.h"
#include "detail/ivf/vq.h"
#include "detail/linalg/matrix.h"
#include "detail/linalg/tdb_io.h"
#include "query_common.h"
#include "utils/utils.h"

TEST_CASE("vq: test test", "[ivf vq]") {
  REQUIRE(true);
}

// vq_apply_query
TEST_CASE("ivf vq: vq apply query", "[ivf vq][ci-skip]") {
  //  vq_apply_query(query, shuffled_db, new_indices, active_queries, ids,
  //  active_partitions, k_nn, first_part, last_part);
  REQUIRE(true);
}

TEST_CASE("ivf vq: infinite all or none", "[ivf vq][ci-skip]") {
  // vq_query_infinite_ram
  // vq_query_infinite_ram_2

  tiledb::Context ctx;

  // auto parts = tdbColMajorMatrix<db_type>(ctx, parts_uri);
  // auto ids = read_vector<uint64_t>(ctx, ids_uri);
  // auto index = sizes_to_indices(sizes);

  auto centroids = tdbColMajorMatrix<db_type>(ctx, centroids_uri);
  centroids.load();
  auto query = tdbColMajorMatrix<db_type>(ctx, query_uri);
  query.load();
  auto index = read_vector<indices_type>(ctx, index_uri);

  SECTION("all") {
    auto nprobe = GENERATE(1, 5);
    auto k_nn = GENERATE(1, 5);
    auto nthreads = GENERATE(1, 5);
    std::cout << nprobe << " " << k_nn << " " << nthreads << std::endl;

    auto&& [D02, I02] = detail::ivf::query_infinite_ram<db_type, ids_type>(
        ctx,
        parts_uri,
        centroids,
        query,
        index,
        ids_uri,
        nprobe,
        k_nn,
        nthreads);

    auto&& [D00, I00] = detail::ivf::vq_query_infinite_ram<db_type, ids_type>(
        ctx,
        parts_uri,
        centroids,
        query,
        index,
        ids_uri,
        nprobe,
        k_nn,
        nthreads);
    auto&& [D01, I01] = detail::ivf::vq_query_infinite_ram_2<db_type, ids_type>(
        ctx,
        parts_uri,
        centroids,
        query,
        index,
        ids_uri,
        nprobe,
        k_nn,
        nthreads);

    CHECK(!std::equal(
        D00.data(),
        D00.data() + D00.size(),
        std::vector<db_type>(D00.size(), 0.0).data()));
    CHECK(!std::equal(
        I00.data(),
        I00.data() + I00.size(),
        std::vector<indices_type>(I00.size(), 0.0).data()));
    CHECK(std::equal(D00.data(), D00.data() + D00.size(), D01.data()));
    CHECK(std::equal(I00.data(), I00.data() + I00.size(), I01.data()));
    CHECK(std::equal(D00.data(), D00.data() + D00.size(), D02.data()));
    CHECK(std::equal(I00.data(), I00.data() + I00.size(), I02.data()));
  }
}

TEST_CASE("ivf vq: finite all or none", "[ivf vq][ci-skip]") {
  // vq_query_infinite_ram
  // vq_query_infinite_ram_2

  tiledb::Context ctx;

  auto upper_bound = GENERATE(2000, 0);
  auto num_queries = GENERATE(1, 0);

  // auto parts = tdbColMajorMatrix<db_type>(ctx, parts_uri);
  // auto ids = read_vector<uint64_t>(ctx, ids_uri);
  // auto index = sizes_to_indices(sizes);

  auto centroids = tdbColMajorMatrix<db_type>(ctx, centroids_uri);
  centroids.load();
  auto query = tdbColMajorMatrix<db_type>(ctx, query_uri, num_queries);
  query.load();
  auto index = read_vector<indices_type>(ctx, index_uri);
  auto groundtruth = tdbColMajorMatrix<groundtruth_type>(ctx, groundtruth_uri);
  groundtruth.load();

  SECTION("all") {
    auto nprobe = GENERATE(5, 1);
    auto k_nn = GENERATE(5, 1);
    auto nthreads = GENERATE(5, 1);
    std::cout << upper_bound << " " << nprobe << " " << num_queries << " "
              << k_nn << " " << nthreads << std::endl;

    auto&& [D00, I00] = detail::ivf::query_infinite_ram<db_type, ids_type>(
        ctx,
        parts_uri,
        centroids,
        query,
        index,
        ids_uri,
        nprobe,
        k_nn,
        nthreads);

    auto&& [D01, I01] = detail::ivf::vq_query_finite_ram<db_type, ids_type>(
        ctx,
        parts_uri,
        centroids,
        query,
        index,
        ids_uri,
        nprobe,
        k_nn,
        upper_bound,
        nthreads);

    auto&& [D02, I02] = detail::ivf::vq_query_finite_ram_2<db_type, ids_type>(
        ctx,
        parts_uri,
        centroids,
        query,
        index,
        ids_uri,
        nprobe,
        k_nn,
        upper_bound,
        nthreads);

    auto&& [D03, I03] = detail::ivf::query_finite_ram<db_type, ids_type>(
        ctx,
        parts_uri,
        centroids,
        query,
        index,
        ids_uri,
        nprobe,
        k_nn,
        upper_bound,
        nthreads);

    CHECK(D00.num_rows() == D01.num_rows());
    CHECK(D00.num_cols() == D01.num_cols());
    CHECK(I00.num_rows() == I01.num_rows());
    CHECK(I00.num_cols() == I01.num_cols());
    CHECK(D00.num_rows() == D02.num_rows());
    CHECK(D00.num_cols() == D02.num_cols());
    CHECK(I00.num_rows() == I02.num_rows());
    CHECK(I00.num_cols() == I02.num_cols());

    auto intersections00 = count_intersections(I00, groundtruth, k_nn);
    auto intersections01 = count_intersections(I01, groundtruth, k_nn);
    auto intersections02 = count_intersections(I02, groundtruth, k_nn);
    auto intersections03 = count_intersections(I03, groundtruth, k_nn);

    CHECK(intersections00 != 0);
    CHECK(intersections00 == intersections01);
    CHECK(intersections00 == intersections02);
    CHECK(intersections00 == intersections03);

    debug_slices_diff(D00, D01, "D00 vs D01");
    debug_slices_diff(D00, D02, "D00 vs D02");
    debug_slices_diff(D00, D03, "D00 vs D03");

    CHECK(!std::equal(
        D00.data(),
        D00.data() + D00.size(),
        std::vector<db_type>(D00.size(), 0.0).data()));
    CHECK(std::equal(D00.data(), D00.data() + D00.size(), D01.data()));
    CHECK(std::equal(D00.data(), D00.data() + D00.size(), D02.data()));
    CHECK(std::equal(D00.data(), D00.data() + D00.size(), D03.data()));
  }
}

// vq_query_finite_ram
// vq_query_finite_ram_2
