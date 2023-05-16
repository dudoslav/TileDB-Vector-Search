/**
 * @file   ivf_index.h
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
 * Header-only library of functions for building an inverted file (IVF) index.
 * Will be used when we start computing our own k-means.
 *
 */

#ifndef TILEDB_IVF_INDEX_H
#define TILEDB_IVF_INDEX_H

#include "flat_query.h"
#include "linalg.h"

enum class InitType { random, kmeanspp };
enum class KMeansAlgorithm { lloyd, elkan };

template <class T>
auto ivf_flat(
    const Matrix<T>& data,
    unsigned nclusters,
    InitType init_type,
    unsigned nrepeats,
    unsigned max_iter,
    double tol,
    size_t seed,
    KMeansAlgorithm algorithm,
    size_t nthreads) {

  Matrix<T> centroids(data.num_rows(), nclusters);
  auto && [lower, upper]= std::minmax_element(data.data(), data.data() + data.num_rows() * data.num_cols());

  // initialize centroids randomly
  std::random_device rd; // create a random device to seed the random number generator
  std::mt19937 gen(rd()); // create a Mersenne Twister engine
  std::uniform_real_distribution<T> dis(*lower, *upper); // create a uniform integer distribution between 0 and 100
  std::generate(data.data(), data.data() + data.num_rows() * data.num_cols(), [&]() { return dis(gen); }); // use std::generate to fill the vector with random integers

  Matrix<size_t> top_k(1, data.num_cols());

  for (size_t iter = 0; iter < max_iter; ++iter) {
    // while not converged and iter < max_iter
    // for each vector in the dataset, find the nearest centroid

    // @todo: Need to make ground_truth optional
    // @todo: Need to return scores
    //query_gemm(centroids, data, dummy, top_k, 1, false, nthreads);

    // compute top_k scores of centroids vs vectors
    //   note that the roles of data vectors and query vectors is reversed,
    //   we want top 1 each vector, not each centroid, i.e., the vectors are
    //   the queries and the centroids are the data
    //   call gemm with centroids as data and vectors as queries (since the #
    //   of queries and # of vectors are >> 1

    // update centroids -> new centroids
    // || new - old || / || old || < tol
    // scikit learn:
    //   Relative tolerance with regards to Frobenius norm of the difference in
    //   cluster centers of two consecutive iterations to declare convergence.
    // https://scikit-learn.org/stable/modules/generated/sklearn.cluster.KMeans.html
  }
}

#endif  // TILEDB_IVF_INDEX_H