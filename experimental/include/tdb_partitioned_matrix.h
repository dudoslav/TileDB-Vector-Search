

#ifndef TILEDB_PARTITIONED_MATRIX_H
#define TILEDB_PARTITIONED_MATRIX_H
#include <cstddef>
#include <future>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include <tiledb/tiledb>
#include "mdspan/mdspan.hpp"

#include "linalg.h"

#include "array_types.h"
#include "utils/timer.h"

namespace stdx {
using namespace Kokkos;
using namespace Kokkos::Experimental;
}  // namespace stdx

extern bool global_verbose;
extern bool global_debug;
extern std::string global_region;

template <class T, class LayoutPolicy = stdx::layout_right, class I = size_t>
class tdbPartitionedMatrix : public Matrix<T, LayoutPolicy, I> {
  /****************************************************************************
   *
   * Duplication from tdbMatrix
   *
   * @todo Unify duplicated code
   *
   ****************************************************************************/
  using Base = Matrix<T, LayoutPolicy, I>;
  using Base::Base;

  using typename Base::index_type;
  using typename Base::reference;
  using typename Base::size_type;

  using view_type = Base;

  constexpr static auto matrix_order_{order_v<LayoutPolicy>};

 private:
  using row_domain_type = int32_t;
  using col_domain_type = int32_t;

  // @todo: Make this configurable
  std::map<std::string, std::string> init_{
      {"vfs.s3.region", global_region.c_str()}};
  tiledb::Config config_{init_};
  tiledb::Context ctx_{config_};

  tiledb::Array array_;
  tiledb::ArraySchema schema_;
  std::unique_ptr<T[]> backing_data_;
  size_t num_array_rows_{0};
  size_t num_array_cols_{0};

  std::tuple<index_type, index_type> row_view_;
  std::tuple<index_type, index_type> col_view_;
  index_type row_offset_{0};
  index_type col_offset_{0};

  std::future<bool> fut_;
  size_t pending_row_offset{0};
  size_t pending_col_offset{0};

  size_t total_num_parts_{0};

  /****************************************************************************
   *
   * Stuff for partitioned (reshuffled) matrix
   *
   * @todo This needs to go into its own class
   *
   ****************************************************************************/
  tiledb::Array ids_array_;
  tiledb::ArraySchema ids_schema_;
  std::vector<shuffled_ids_type> indices_;  // @todo pointer and span?
  std::vector<parts_type> parts_;           // @todo pointer and span?
  std::vector<shuffled_ids_type> ids_;      // @todo pointer and span?

  std::tuple<index_type, index_type> row_part_view_;
  std::tuple<index_type, index_type> col_part_view_;

  index_type row_part_offset_{0};
  index_type col_part_offset_{0};

  size_t max_cols_{0};
  size_t num_cols_{0};

  size_t max_col_parts_{0};
  size_t num_col_parts_{0};

 public:
  tdbPartitionedMatrix(
      const std::string& uri,
      std::vector<indices_type>&& indices,
      const std::vector<parts_type>& parts,
      const std::string& id_uri,
      // std::vector<shuffled_ids_type>& shuffled_ids,
      size_t nthreads)
      : tdbPartitionedMatrix(
            uri, indices, parts, id_uri, /*shuffled_ids,*/ 0, nthreads) {
  }

  /**
   * Gather pieces of a partitioned array into a single array (along with the
   * vector ids into a corresponding 1D array)
   *
   * @todo Column major is kind of baked in here.  Need to generalize.
   */
  tdbPartitionedMatrix(
      const std::string& uri,
      std::vector<indices_type>&& in_indices,
      const std::vector<parts_type>& in_parts,
      const std::string& ids_uri,
      // std::vector<shuffled_ids_type>& shuffled_ids,
      size_t upper_bound,
      size_t nthreads)
      : array_{ctx_, uri, TILEDB_READ}
      , schema_{array_.schema()}
      , ids_array_{ctx_, ids_uri, TILEDB_READ}
      , ids_schema_{ids_array_.schema()}
      , indices_{std::move(in_indices)}
      , parts_{in_parts}
      , row_part_view_{0, 0}
      , col_part_view_{0, 0} {
    total_num_parts_ = size(parts_);

    life_timer _{"Initialize tdb partitioned matrix " + uri};

    auto cell_order = schema_.cell_order();
    auto tile_order = schema_.tile_order();

    // @todo Maybe throw an exception here?  Have to properly handle since
    // this is a constructor.
    assert(cell_order == tile_order);

    auto domain_{schema_.domain()};

    auto array_rows_{domain_.dimension(0)};
    auto array_cols_{domain_.dimension(1)};

    num_array_rows_ =
        (array_rows_.template domain<row_domain_type>().second -
         array_rows_.template domain<row_domain_type>().first + 1);
    num_array_cols_ =
        (array_cols_.template domain<col_domain_type>().second -
         array_cols_.template domain<col_domain_type>().first + 1);

    if ((matrix_order_ == TILEDB_ROW_MAJOR && cell_order == TILEDB_COL_MAJOR) ||
        (matrix_order_ == TILEDB_COL_MAJOR && cell_order == TILEDB_ROW_MAJOR)) {
      throw std::runtime_error("Cell order and matrix order must match");
    }

    size_t dimension = num_array_rows_;

    if (indices_[size(indices_) - 1] == indices_[size(indices_) - 2]) {
      if (indices_[size(indices_) - 1] > num_array_cols_) {
        throw std::runtime_error("Indices are not valid");
      }
      indices_[size(indices_) - 1] = num_array_cols_;
    }

    auto total_max_cols = 0;
    for (size_t i = 0; i < total_num_parts_; ++i) {
      total_max_cols += indices_[parts_[i] + 1] - indices_[parts_[i]];
    }

    if (upper_bound == 0 || upper_bound > total_max_cols) {
      max_cols_ = total_max_cols;
    } else {
      max_cols_ = upper_bound;
    }

    // @todo be more sensible -- dont use a vector and don't use inout
    if (size(ids_) < max_cols_) {
      ids_.resize(max_cols_);
    }

#ifndef __APPLE__
    auto data_ = std::make_unique_for_overwrite<T[]>(dimension * max_cols_);
#else
    auto data_ = std::unique_ptr<T[]>(new T[dimension * max_cols_]);
#endif

    Base::operator=(Base{std::move(data_), dimension, max_cols_});

    // @todo Take this out
    advance();
  }

  /**
   * Read in the next partitions
   */

  bool advance() {
    // @todo -- col oriented only for now -- generalize!!

    {
      const size_t attr_idx = 0;
      auto attr = schema_.attribute(attr_idx);

      std::string attr_name = attr.name();
      tiledb_datatype_t attr_type = attr.type();
      if (attr_type != tiledb::impl::type_to_tiledb<T>::tiledb_type) {
        throw std::runtime_error(
            "Attribute type mismatch: " + std::to_string(attr_type) + " != " +
            std::to_string(tiledb::impl::type_to_tiledb<T>::tiledb_type));
      }

      auto dimension = num_array_rows_;

      /*
       * Fit as many partitions as we can into max_cols_
       */
      std::get<0>(col_view_) = std::get<1>(col_view_);  // # columns
      std::get<0>(col_part_view_) =
          std::get<1>(col_part_view_);  // # partitions

      std::get<1>(col_part_view_) = std::get<0>(col_part_view_);
      for (size_t i = std::get<0>(col_part_view_); i < total_num_parts_; ++i) {
        auto next_part_size = indices_[parts_[i] + 1] - indices_[parts_[i]];
        if ((std::get<1>(col_view_) + next_part_size) >
            std::get<0>(col_view_) + max_cols_) {
          break;
        }
        std::get<1>(col_view_) += next_part_size;
        std::get<1>(col_part_view_) = i + 1;
      }
      num_cols_ = std::get<1>(col_view_) - std::get<0>(col_view_);
      col_offset_ = std::get<0>(col_view_);

      num_col_parts_ =
          std::get<1>(col_part_view_) - std::get<0>(col_part_view_);
      col_part_offset_ = std::get<0>(col_part_view_);

      if ((num_cols_ == 0 && num_col_parts_ != 0) ||
          (num_cols_ != 0 && num_col_parts_ == 0)) {
        throw std::runtime_error("Invalid partitioning");
      }
      if (num_cols_ == 0) {
        return false;
      }

      /*
       * Set up the subarray to read the partitions
       */
      tiledb::Subarray subarray(ctx_, this->array_);

      // Dimension 0 goes from 0 to 127
      subarray.add_range(0, 0, (int)dimension - 1);

      /**
       * Read in the next batch of partitions
       */
      size_t col_count = 0;
      for (size_t j = std::get<0>(col_part_view_);
           j < std::get<1>(col_part_view_);
           ++j) {
        size_t start = indices_[parts_[j]];
        size_t stop = indices_[parts_[j] + 1];
        size_t len = stop - start;
        if (len == 0) {
          continue;
        }
        col_count += len;
        subarray.add_range(1, (int)start, (int)stop - 1);
      }
      if (col_count != std::get<1>(col_view_) - std::get<0>(col_view_)) {
        throw std::runtime_error("Column count mismatch");
      }

      auto cell_order = schema_.cell_order();
      auto layout_order = cell_order;

      tiledb::Query query(ctx_, this->array_);

      // auto ptr = data_.get();

      auto ptr = this->data();
      query.set_subarray(subarray)
          .set_layout(layout_order)
          .set_data_buffer(attr_name, ptr, col_count * dimension);
      query.submit();

      // assert(tiledb::Query::Status::COMPLETE == query.query_status());
      if (tiledb::Query::Status::COMPLETE != query.query_status()) {
        throw std::runtime_error("Query status is not complete -- fix me");
      }
    }

    /**
     * Now deal with ids
     */
    {
      auto ids_attr_idx = 0;

      auto ids_attr = ids_schema_.attribute(ids_attr_idx);
      std::string ids_attr_name = ids_attr.name();

      tiledb::Subarray ids_subarray(ctx_, ids_array_);

      size_t ids_col_count = 0;
      for (size_t j = std::get<0>(col_part_view_);
           j < std::get<1>(col_part_view_);
           ++j) {
        size_t start = indices_[parts_[j]];
        size_t stop = indices_[parts_[j] + 1];
        size_t len = stop - start;
        if (len == 0) {
          continue;
        }
        ids_col_count += len;
        ids_subarray.add_range(0, (int)start, (int)stop - 1);
      }
      if (ids_col_count != std::get<1>(col_view_) - std::get<0>(col_view_)) {
        throw std::runtime_error("Column count mismatch");
      }

      tiledb::Query ids_query(ctx_, ids_array_);

      auto ids_ptr = ids_.data();
      ids_query.set_subarray(ids_subarray)
          .set_data_buffer(ids_attr_name, ids_ptr, ids_col_count);
      ids_query.submit();

      // assert(tiledb::Query::Status::COMPLETE == query.query_status());
      if (tiledb::Query::Status::COMPLETE != ids_query.query_status()) {
        throw std::runtime_error("Query status is not complete -- fix me");
      }
    }

    return true;
  }

  auto& ids() const {
    return ids_;
  }

  index_type num_col_parts() const {
    return std::get<1>(col_part_view_) - std::get<0>(col_part_view_);
  }

  index_type col_part_offset() const {
    return col_part_offset_;
  }

  index_type col_offset() const {
    return col_offset_;
  }

  /**
   * Destructor.  Closes arrays if they are open.
   */
  ~tdbPartitionedMatrix() {
    if (array_.is_open()) {
      array_.close();
    }
    if (ids_array_.is_open()) {
      ids_array_.close();
    }
  }
};

/**
 * Convenience class for row-major matrices.
 */
template <class T, class I = size_t>
using tdbRowMajorPartitionedMatrix =
    tdbPartitionedMatrix<T, stdx::layout_right, I>;

/**
 * Convenience class for column-major matrices.
 */
template <class T, class I = size_t>
using tdbColMajorPartitionedMatrix =
    tdbPartitionedMatrix<T, stdx::layout_left, I>;

#endif  // TILEDB_PARTITIONED_MATRIX_H