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
 * Header-only library of functions for building an inverted file (IVF) index,
 * generated by kmeans algorithm.
 *
 * The basic use case is:
 * - Create an instance of the index
 * - Call train() to build the index
 * - OR Call load() to load the index from TileDB arrays
 * - Call add() to add vectors to the index (alt. add with ids)
 * - Call search() to query the index, returning the ids of the nearest vectors,
 *   and optionally the distances.
 * - Compute the recall of the search results.
 *
 * - Call save() to save the index to disk
 * - Call reset() to clear the index
 *
 * Still WIP.  Currently the class generates centroids with kmeans, using
 * either random or kmeans++ initialization.
 */

#ifndef TILEDB_IVF_INDEX_H
#define TILEDB_IVF_INDEX_H

#include <atomic>
#include <random>
#include <thread>

#include "algorithm.h"
#include "linalg.h"

#include "detail/flat/qv.h"
#include "detail/ivf/index.h"

/**
 * Enumeration of initialization methods for kmeans.
 */
enum class kmeans_init { none, kmeanspp, random };

/**
 * @brief A class for building an inverted file (IVF) index, generated by
 * the kmeans algorithm.  User can choose to initialize centroids using
 * kmeans++ or random selection.
 *
 * This class is still WIP.  It currently generates centroids with kmeans
 * (which has been verified against scikit_learn but does not yet create
 * the entire index.  Index creation will be the subject of a future PR,
 * and is currently being implemented in branch lums/sc-32521/pq.
 *
 * @tparam T The type of the features in the feature vector array
 * @tparam shuffled_ids_type The type of the ids in the partitioned array
 * @tparam indices_type The type used in the index array (that demarcates
 *   partitions).
 */
template <
    class T,
    class shuffled_ids_type = size_t,
    class indices_type = size_t>
class kmeans_index {
  std::mt19937 gen;

  size_t dimension_{0};
  size_t nlist_;
  size_t max_iter_;
  double tol_;
  double reassign_ratio_{0.075};
  size_t nthreads_{std::thread::hardware_concurrency()};

  ColMajorMatrix<T> centroids_;
  std::vector<indices_type> indices_;
  std::vector<shuffled_ids_type> shuffled_ids_;
  ColMajorMatrix<T> shuffled_db_;

 public:
  using value_type = T;
  using index_type = indices_type;

  /**
   * @brief Construct a new kmeans index object, setting a number of parameters
   * to be used subsequently in training.
   * @param dimension Dimension of the vectors comprising the training set and
   * the data set.
   * @param nlist Number of centroids / partitions to compute.
   * @param max_iter Maximum number of iterations for kmans algorithm.
   * @param tol Convergence tolerance for kmeans algorithm.
   * @param nthreads Number of threads to use when executing in parallel.
   * @param seed Seed for random number generator.
   */
  kmeans_index(
      size_t dimension,
      size_t nlist,
      size_t max_iter,
      double tol = 0.000025,
      std::optional<size_t> nthreads = std::nullopt,
      std::optional<unsigned int> seed = std::nullopt)
      : gen(seed ? *seed : std::random_device{}())
      , dimension_(dimension)
      , nlist_(nlist)
      , max_iter_(max_iter)
      , tol_(tol)
      , nthreads_(nthreads ? *nthreads : std::thread::hardware_concurrency())
      , centroids_(dimension, nlist) {
  }

  /**
   * @brief Use the kmeans++ algorithm to choose initial centroids.
   * The current implementation follows the algorithm described in
   * the literature (Arthur and Vassilvitskii, 2007):
   *
   *  1a. Choose an initial centroid uniformly at random from the training set
   *  1b. Choose the next centroid from the training set
   *      2b. For each data point x not chosen yet, compute D(x), the distance
   *          between x and the nearest centroid that has already been chosen.
   *      3b. Choose one new data point at random as a new centroid, using a
   *          weighted probability distribution where a point x is chosen
   *          with probability proportional to D(x)2.
   *  3. Repeat Steps 2b and 3b until k centers have been chosen.
   *
   *  The initial centroids are stored in the member variable `centroids_`.
   *  It is expected that the centroids will be further refined by the
   *  kmeans algorithm.
   *
   * @param training_set Array of vectors to cluster.
   *
   * @todo Implement greedy kmeans++: choose several new centers during each
   * iteration, and then greedily chose the one that most decreases φ
   * @todo Finish implementation using triangle inequality.
   */
  void kmeans_pp(const ColMajorMatrix<T>& training_set) {
    scoped_timer _{__FUNCTION__};

    std::uniform_int_distribution<> dis(0, training_set.num_cols() - 1);
    auto choice = dis(gen);

    std::copy(
        begin(training_set[choice]),
        end(training_set[choice]),
        begin(centroids_[0]));

    // Initialize distances, leaving some room to grow
    std::vector<double> distances(
        training_set.num_cols(), std::numeric_limits<double>::max() / 8192);

// Experimental code to use triangle inequality to speed up kmeans++.
#ifdef _TRIANGLE_INEQUALITY
    std::vector<double> centroid_centroid(nlist_, 0.0);
    std::vector<size_t> nearest_centroid(training_set.num_cols(), 0);
#endif

    // Calculate the remaining centroids using K-means++ algorithm
    for (size_t i = 1; i < nlist_; ++i) {
      stdx::execution::indexed_parallel_policy par{nthreads_};
      stdx::range_for_each(
          std::move(par),
          training_set,
          [this, &distances, i](auto&& vec, size_t n, size_t j) {

      // Note: centroid i-1 is the newest centroid

// Experimental code to use triangle inequality to speed up kmeans++.
#ifdef _TRIANGLE_INEQUALITY
            // using triangle inequality, only need to calculate distance to the
            // newest centroid if distance between vec and its current nearest
            // centroid is greater than half the distance between the newest
            // centroid and vectors nearest centroid (1/4 distance squared)

            double min_distance = distances[j];
            if (centroid_centroid[nearest_centroid[j]] < 4 * min_distance) {
              double distance = sum_of_squares(vec, centroids_[i - 1]);
              if (distance < min_distance) {
                min_distance = distance;
                nearest_centroid[j] = i - 1;
                distances[j] = min_distance;
              }
            }
#else
            double distance = sum_of_squares(vec, centroids_[i - 1]);
            auto min_distance = std::min(distances[j], distance);
            distances[j] = min_distance;
#endif
          });

      // Select the next centroid based on the probability proportional to
      // distance squared -- note we did not normalize the vectors ourselves
      // since `discrete_distribution` implicitly does that for us
      std::discrete_distribution<size_t> probabilityDistribution(
          distances.begin(), distances.end());
      size_t nextIndex = probabilityDistribution(gen);
      std::copy(
          begin(training_set[nextIndex]),
          end(training_set[nextIndex]),
          begin(centroids_[i]));
      distances[nextIndex] = 0.0;

// Experimental code to use triangle inequality to speed up kmeans++.
#ifdef _TRIANGLE_INEQUALITY
      // Update centroid-centroid distances -- only need distances from each
      // existing to the new one
      centroid_centroid[i] = sum_of_squares(centroids_[i], centroids_[i - 1]);
      for (size_t j = 0; j < i; ++j) {
        centroid_centroid[j] = sum_of_squares(centroids_[i], centroids_[j]);
      }
#endif
    }
  }

  /**
   * @brief Initialize centroids by choosing them at random from training set.
   * @param training_set Array of vectors to cluster.
   */
  void kmeans_random_init(const ColMajorMatrix<T>& training_set) {
    scoped_timer _{__FUNCTION__};

    std::vector<size_t> indices(nlist_);
    std::vector<bool> visited(training_set.num_cols(), false);
    std::uniform_int_distribution<> dis(0, training_set.num_cols() - 1);
    for (size_t i = 0; i < nlist_; ++i) {
      size_t index;
      do {
        index = dis(gen);
      } while (visited[index]);
      indices[i] = index;
      visited[index] = true;
    }
    // Another approach to generating random indices would be to shuffle [0, N)
    // std::iota(begin(indices), end(indices), 0);
    // std::shuffle(begin(indices), end(indices), gen);

    for (size_t i = 0; i < nlist_; ++i) {
      std::copy(
          begin(training_set[indices[i]]),
          end(training_set[indices[i]]),
          begin(centroids_[i]));
    }
  }

  /**
   * @brief Use kmeans algorithm to cluster vectors into centroids.  Beginning
   * with an initial set of centroids, the algorithm iteratively partitions
   * the training_set into clusters, and then recomputes new centroids based
   * on the clusters.  The algorithm terminates when the change in centroids
   * is less than a threshold tolerance, or when a maximum number of
   * iterations is reached.
   *
   * @param training_set Array of vectors to cluster.
   */
  void train_no_init(const ColMajorMatrix<T>& training_set) {
    scoped_timer _{__FUNCTION__};

    std::vector<size_t> degrees(nlist_, 0);
    ColMajorMatrix<T> new_centroids(dimension_, nlist_);

    for (size_t iter = 0; iter < max_iter_; ++iter) {
      auto [scores, parts] = detail::flat::qv_partition_with_scores(
          centroids_, training_set, nthreads_);
      // auto parts = detail::flat::qv_partition(centroids_, training_set,
      // nthreads_);

      std::fill(
          new_centroids.data(),
          new_centroids.data() +
              new_centroids.num_rows() * new_centroids.num_cols(),
          0.0);
      std::fill(begin(degrees), end(degrees), 0);

      // How many centroids should we try to fix up
      size_t heap_size = std::ceil(reassign_ratio_ * nlist_) + 5;
      auto high_scores =
          fixed_min_pair_heap<value_type, index_type, std::greater<value_type>>(
              heap_size, std::greater<value_type>());
      auto low_degrees = fixed_min_pair_heap<index_type, index_type>(heap_size);

      // @todo parallelize -- by partition
      for (size_t i = 0; i < training_set.num_cols(); ++i) {
        auto part = parts[i];
        auto centroid = new_centroids[part];
        auto vector = training_set[i];
        // std::copy(begin(vector), end(vector), begin(centroid));
        for (size_t j = 0; j < dimension_; ++j) {
          centroid[j] += vector[j];
        }
        ++degrees[part];
        high_scores.insert(scores[i], i);
      }

      size_t max_degree = 0;
      for (size_t i = 0; i < nlist_; ++i) {
        auto degree = degrees[i];
        max_degree = std::max<size_t>(max_degree, degree);
        low_degrees.insert(degree, i);
      }
      size_t lower_degree_bound = std::ceil(max_degree * reassign_ratio_);

      // Don't reassign if we are on last iteration
      if (iter != max_iter_ - 1) {
// An alternative strategy for reassignment.  The other strategy seems to
// work better.  Keeping this temporarily as reference to verify.
#if 0
        // Pick a random vector to be a new centroid
        std::uniform_int_distribution<> dis(0, training_set.num_cols() - 1);
        for (auto&& [degree, zero_part] : low_degrees) {
          if (degree < lower_degree_bound) {
            auto index = dis(gen);
            auto rand_vector = training_set[index];
            auto low_centroid = new_centroids[zero_part];
            std::copy(begin(rand_vector), end(rand_vector), begin(low_centroid));
            for (size_t i = 0; i < dimension_; ++i) {
              new_centroids[parts[index]][i] -= rand_vector[i];
            }
          }
        }
#endif
        // Move vectors with high scores to replace zero-degree partitions
        std::sort_heap(begin(low_degrees), end(low_degrees));
        std::sort_heap(
            begin(high_scores), end(high_scores), [](auto a, auto b) {
              return std::get<0>(a) > std::get<0>(b);
            });
        for (size_t i = 0; i < size(low_degrees) &&
                           std::get<0>(low_degrees[i]) <= lower_degree_bound;
             ++i) {
          // std::cout << "i: " << i << " low_degrees: ("
          //           << std::get<1>(low_degrees[i]) << " "
          //           << std::get<0>(low_degrees[i]) << ") high_scores: ("
          //           << parts[std::get<1>(high_scores[i])] << " "
          //          << std::get<1>(high_scores[i]) << " "
          //          << std::get<0>(high_scores[i]) << ")" << std::endl;
          auto [degree, zero_part] = low_degrees[i];
          auto [score, high_vector_id] = high_scores[i];
          auto low_centroid = new_centroids[zero_part];
          auto high_vector = training_set[high_vector_id];
          std::copy(begin(high_vector), end(high_vector), begin(low_centroid));
          for (size_t i = 0; i < dimension_; ++i) {
            new_centroids[parts[high_vector_id]][i] -= high_vector[i];
          }
          ++degrees[zero_part];
          --degrees[parts[high_vector_id]];
        }
      }
      /**
       * Check for convergence
       */
      // @todo parallelize?
      double max_diff = 0.0;
      double total_weight = 0.0;
      for (size_t j = 0; j < nlist_; ++j) {
        if (degrees[j] != 0) {
          auto centroid = new_centroids[j];
          for (size_t k = 0; k < dimension_; ++k) {
            centroid[k] /= degrees[j];
            total_weight += centroid[k] * centroid[k];
          }
        }
        auto diff = sum_of_squares(centroids_[j], new_centroids[j]);
        max_diff = std::max<double>(max_diff, diff);
      }
      centroids_.swap(new_centroids);
      // std::cout << "max_diff: " << max_diff << " total_weight: " <<
      // total_weight
      //          << std::endl;
      if (max_diff < tol_ * total_weight) {
        // std::cout << "Converged after " << iter << " iterations." <<
        // std::endl;
        break;
      }

// Printf debugging code
#if 0
        auto mm = std::minmax_element(begin(degrees), end(degrees));
        double sum = std::accumulate(begin(degrees), end(degrees), 0);
        double average = sum / (double)size(degrees);

        auto min = *mm.first;
        auto max = *mm.second;
        auto diff = max - min;
        std::cout << "avg: " << average << " sum: " << sum << " min: " << min
                  << " max: " << max << " diff: " << diff << std::endl;
#endif
    }

// Printf debugging code to save partitions to disk for external tools
#ifdef _SAVE_PARTITIONS
    {
      char tempFileName[L_tmpnam];
      tmpnam(tempFileName);

      std::ofstream file(tempFileName);
      if (!file) {
        std::cout << "Error opening the file." << std::endl;
        return;
      }

      for (const auto& element : degrees) {
        file << element << ',';
      }
      file << std::endl;

      for (auto s = 0; s < training_set.num_cols(); ++s) {
        for (auto t = 0; t < training_set.num_rows(); ++t) {
          file << std::to_string(training_set(t, s)) << ',';
        }
        file << std::endl;
      }
      file << std::endl;

      for (auto s = 0; s < centroids_.num_cols(); ++s) {
        for (auto t = 0; t < centroids_.num_rows(); ++t) {
          file << std::to_string(centroids_(t, s)) << ',';
        }
        file << std::endl;
      }

      file.close();

      std::cout << "Data written to file: " << tempFileName << std::endl;
    }
#endif
  }

  /*
   * @brief A function used for comparison with centroids generated with
   * scikit_learn's kmeans algorithm.
   * @param centroids
   * @param vectors
   * @return
   */
  static std::vector<indices_type> predict(
      const ColMajorMatrix<T>& centroids, const ColMajorMatrix<T>& vectors) {
    // Return a vector of indices of the nearest centroid for each vector in
    // the matrix. Write the code below:
    auto nClusters = centroids.num_cols();
    std::vector<indices_type> indices(vectors.num_cols());
    std::vector<float> distances(nClusters);
    for (size_t i = 0; i < vectors.num_cols(); ++i) {
      for (size_t j = 0; j < nClusters; ++j) {
        distances[j] = sum_of_squares(vectors[i], centroids[j]);
      }
      indices[i] =
          std::min_element(begin(distances), end(distances)) - begin(distances);
    }
    return indices;
  }

  /**
   * Compute centroids and partitions of the training set data.
   * @param training_set Array of vectors to cluster.
   * @param init Specify which initialization algorithm to use,
   * random (`random`) or kmeans++ (`kmeanspp`).
   */
  void train(const ColMajorMatrix<T>& training_set, kmeans_init init) {
    switch (init) {
      case (kmeans_init::none):
        break;
      case (kmeans_init::kmeanspp):
        kmeans_pp(training_set);
        break;
      case (kmeans_init::random):
        kmeans_random_init(training_set);
        break;
    };

// Printf debugging code
#if 0
    std::cout << "\nCentroids Before:\n" << std::endl;
    for (size_t j = 0; j < centroids_.num_cols(); ++j) {
      for (size_t i = 0; i < dimension_; ++i) {
        std::cout << centroids_[j][i] << " ";
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;
#endif

    train_no_init(training_set);

// Printf debugging code
#if 0
    std::cout << "\nCentroids After:\n" << std::endl;
    for (size_t j = 0; j < centroids_.num_cols(); ++j) {
      for (size_t i = 0; i < dimension_; ++i) {
        std::cout << centroids_[j][i] << " ";
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;
#endif
  }

// @todo WIP to generate complete index (implemented in lums/sc-32521/pq)
#if 0
  void add(const ColMajorMatrix<T>& db) {
    auto parts = detail::flat::qv_partition(centroids_, db, nthreads_);
    std::vector<size_t> degrees(centroids_.num_cols());
    std::vector<indices_type> indices(centroids_.num_cols() + 1);
    std::vector shuffled_ids = std::vector<shuffled_ids_type>(db.num_cols());
    auto shuffled_db = ColMajorMatrix<T>{db.num_rows(), db.num_cols()};

    for (size_t i = 0; i < db.num_cols(); ++i) {
      auto j = parts[i];
      ++degrees[j];
    }
    indices[0] = 0;
    std::inclusive_scan(begin(degrees), end(degrees), begin(indices) + 1);

    std::iota(begin(shuffled_ids), end(shuffled_ids), 0);

    for (size_t i = 0; i < db.num_cols(); ++i) {
      size_t bin = parts[i];
      size_t ibin = indices[bin];

      shuffled_ids[ibin] = i;

      assert(ibin < shuffled_db.num_cols());
      for (size_t j = 0; j < db.num_rows(); ++j) {
        shuffled_db(j, ibin) = db(j, i);
      }
      ++indices[bin];
    }

    std::shift_right(begin(indices), end(indices), 1);
    indices[0] = 0;

    indices_ = std::move(indices);
    shuffled_ids_ = std::move(shuffled_ids);
    shuffled_db_ = std::move(shuffled_db);
  }
#endif

  auto set_centroids(const ColMajorMatrix<T>& centroids) {
    std::copy(
        centroids.data(),
        centroids.data() + centroids.num_rows() * centroids.num_cols(),
        centroids_.data());
  }

  auto& get_centroids() {
    return centroids_;
  }
};

#endif  // TILEDB_IVF_INDEX_H