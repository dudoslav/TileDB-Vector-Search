/**
 * @file   linalg.h
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
 * Header-only library of some basic linear algebra data structures and
 * operations. Uses C++23 reference implementation of mdspan from Sandia
 * National Laboratories.
 *
 * The data structures here are lightweight wrappers around span (1-D) or
 * mdspan (2-D).  Their primary purpose is to provide classes that own
 * their storage while also presenting the span and mdspan interfaces.
 *
 * The tdbMatrix class is derived from Matrix, but gets its initial data
 * from a given TileDB array.
 *
 */

#ifndef TDB_LINALG_H
#define TDB_LINALG_H

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <mdspan/mdspan.hpp>
#include <tiledb/tiledb>

#include "timer.h"


extern bool global_verbose;
extern bool global_debug;
extern std::string global_region;

/**
 * @brief A 1-D vector class that owns its storage.
 * @tparam T
 *
 * @todo More verification.  Replace uses of std::vector with this class.
 */
template <class T>
class Vector : public std::span<T> {
  using Base = std::span<T>;
  using Base::Base;

 public:
  using index_type = typename Base::difference_type;
  using size_type = typename Base::size_type;
  using reference = typename Base::reference;

 private:
  size_type nrows_;
  std::unique_ptr<T[]> storage_;

 public:
  Vector(index_type nrows) noexcept
      : nrows_(nrows)
      , storage_{new T[nrows_]} {
    Base::operator=(Base{storage_.get(), nrows_});
  }

  Vector(index_type nrows, std::unique_ptr<T[]> storage)
      : nrows_(nrows)
      , storage_{std::move(storage)} {
    Base::operator=(Base{storage_.get(), nrows_});
  }

  Vector(Vector&& rhs)
      : nrows_{rhs.nrows_}
      , storage_{std::move(rhs.storage_)} {
    Base::operator=(Base{storage_.get(), nrows_});
  }

  constexpr reference operator()(size_type idx) const noexcept {
    return Base::operator[](idx);
  }

  constexpr size_type num_rows() const noexcept {
    return nrows_;
  }
};

template <class I = size_t>
using matrix_extents = Kokkos::dextents<I, 2>;

/**
 * @brief A 2-D matrix class that owns its storage.  The interface is
 * that of mdspan.
 *
 * @tparam T
 * @tparam LayoutPolicy
 * @tparam I
 *
 * @todo Make an alias for extents.
 */
template <class T, class LayoutPolicy = Kokkos::layout_right, class I = size_t>
class Matrix : public Kokkos::mdspan<T, matrix_extents<I>, LayoutPolicy> {
  using Base = Kokkos::mdspan<T, matrix_extents<I>, LayoutPolicy>;
  //using Base::Base;

 public:
  using layout_policy = LayoutPolicy;
  using index_type = typename Base::index_type;
  using size_type = typename Base::size_type;
  using reference = typename Base::reference;

 private:
  size_type nrows_{0};
  size_type ncols_{0};

  std::unique_ptr<T[]> storage_;

 public:
  Matrix() noexcept = default;

  Matrix(
      size_type nrows,
      size_type ncols,
      LayoutPolicy policy = LayoutPolicy()) noexcept
      : nrows_(nrows)
      , ncols_(ncols)
      , storage_{new T[nrows_ * ncols_]} {
    Base::operator=(Base{storage_.get(), nrows_, ncols_});
  }

  Matrix(
      std::unique_ptr<T[]>&& storage,
      size_type nrows,
      size_type ncols,
      LayoutPolicy policy = LayoutPolicy()) noexcept
      : nrows_(nrows)
      , ncols_(ncols)
      , storage_{std::move(storage)} {
    Base::operator=(Base{storage_.get(), nrows_, ncols_});
  }

  Matrix(Matrix&& rhs)
      : nrows_{rhs.nrows_}
      , ncols_{rhs.ncols_}
      , storage_{std::move(rhs.storage_)} {
    Base::operator=(Base{storage_.get(), nrows_, ncols_});
  }

  auto& operator=(Matrix&& rhs) {
    nrows_ = rhs.nrows_;
    ncols_ = rhs.ncols_;
    storage_ = std::move(rhs.storage_);
    Base::operator=(Base{storage_.get(), nrows_, ncols_});
    return *this;
  }

  auto data() {
    return storage_.get();
  }

  auto data() const {
    return storage_.get();
  }

  auto raveled() {
    return std::span(storage_.get(), nrows_ * ncols_);
  }

  auto raveled() const {
    return std::span(storage_.get(), nrows_ * ncols_);
  }

  auto operator[](index_type i) {
    if constexpr (std::is_same_v<LayoutPolicy, Kokkos::layout_right>) {
      return std::span(&Base::operator()(i, 0), ncols_);
    } else {
      return std::span(&Base::operator()(0, i), nrows_);
    }
  }

  auto operator[](index_type i) const {
    if constexpr (std::is_same_v<LayoutPolicy, Kokkos::layout_right>) {
      return std::span(&Base::operator()(i, 0), ncols_);
    } else {
      return std::span(&Base::operator()(0, i), nrows_);
    }
  }

  auto rank() const noexcept {
    return Base::extents().rank();
    // return 2;  //
  }

  auto span() const noexcept {
    if constexpr (std::is_same_v<LayoutPolicy, Kokkos::layout_right>) {
      return num_cols();
    } else {
      return num_rows();
    }
  }

  auto num_rows() const noexcept {
    return nrows_;
  }

  auto num_cols() const noexcept {
    return ncols_;
  }
};

/**
 * Convenience class for row-major matrices.
 */
template <class T, class I = size_t>
using RowMajorMatrix = Matrix<T, Kokkos::layout_right, I>;

/**
 * Convenience class for column-major matrices.
 */
template <class T, class I = size_t>
using ColMajorMatrix = Matrix<T, Kokkos::layout_left, I>;

/**
 * Convenience class for turning 2D matrices into 1D vectors.
 */
template <class T, class LayoutPolicy = Kokkos::layout_right, class I = size_t>
auto raveled(Matrix<T, LayoutPolicy, I>& m) {
  return m.raveled();
}

/**
 * Derived from `Matrix`.  Initialized in construction by filling from a given
 * TileDB array.
 *
 * @todo Evaluate whether or not we really need to do things this way or if
 * it is sufficient to simply have one Matrix class and have a factory that
 * creates them by reading from TileDB.
 */
template <class T, class LayoutPolicy = Kokkos::layout_right, class I = size_t>
class tdbMatrix : public Matrix<T, LayoutPolicy, I> {
  using Base = Matrix<T, LayoutPolicy, I>;
  using Base::Base;

 public:
  using index_type = typename Base::index_type;
  using size_type = typename Base::size_type;
  using reference = typename Base::reference;

 private:
  // @todo: Make this configurable
  std::map<std::string, std::string> init_{{"vfs.s3.region", global_region.c_str()}};
  tiledb::Config config_{init_};
  tiledb::Context ctx_{config_};

  tiledb::Array array_;
  tiledb::ArraySchema schema_;

 public:
  /**
   * @brief Construct a new tdbMatrix object, limited to `num_elts` vectors.
   * In this case, the `Matrix` is row-major, so the number of vectors is
   * the number of rows.
   *
   * @param uri URI of the TileDB array to read.
   * @param num_elts Number of vectors to read from the array.
   */
  tdbMatrix(const std::string& uri, size_t num_elts) noexcept
      requires(std::is_same_v<LayoutPolicy, Kokkos::layout_right>)
      : tdbMatrix(uri, num_elts, 0) {
  }

  /**
   * @brief Construct a new tdbMatrix object, limited to `num_elts` vectors.
   * In this case, the `Matrix` is column-major, so the number of vectors is
   * the number of columns.
   *
   * @param uri URI of the TileDB array to read.
   * @param num_elts Number of vectors to read from the array.
   */
  tdbMatrix(const std::string& uri, size_t num_elts) noexcept
      requires(std::is_same_v<LayoutPolicy, Kokkos::layout_left>)
      : tdbMatrix(uri, 0, num_elts) {
  }

  /**
   * @brief Construct a new tdbMatrix object, reading all of the vectors in
   * the array.
   *
   * @param uri URI of the TileDB array to read.
   */
  tdbMatrix(const std::string& uri) noexcept
      // requires (std::is_same_v<LayoutPolicy, Kokkos::layout_left>)
      : tdbMatrix(uri, 0, 0) {
    if (global_debug) {
      std::cerr << "# tdbMatrix constructor: " << uri << std::endl;
    }
  }

  /**
   * @brief Construct a new tdbMatrix object, reading a subset of the vectors
   * and a subset of the elements in each vector.
   *
   * @param uri
   * @param num_rows
   * @param num_cols
   */
  tdbMatrix(const std::string& uri, size_t num_rows, size_t num_cols) noexcept
      : tdbMatrix(uri, 0, num_rows, 0, num_cols) {
  }

  /**
   * @brief General constructor.  Read a view of the array, delimited by the
   * given row and column indices.
   *
   * @param uri
   * @param row_begin
   * @param row_end
   * @param col_begin
   * @param col_end
   *
   * @todo Make this compatible with various schemas we are using
   */
  tdbMatrix(
      const std::string& uri,
      size_t row_begin,
      size_t row_end,
      size_t col_begin,
      size_t col_end) noexcept
      : array_{ctx_, uri, TILEDB_READ}
      , schema_{array_.schema()} {

    life_timer _{"read matrix " + uri};
    using row_domain_type = int32_t;
    using col_domain_type = int32_t;

    auto cell_order = schema_.cell_order();
    auto tile_order = schema_.tile_order();

    // @todo Maybe throw an exception here?  Have to properly handle since this
    // is a constructor.
    assert (cell_order == tile_order);


    const size_t idx = 0;

    auto domain_{schema_.domain()};

    auto array_rows_{domain_.dimension(0)};
    auto array_cols_{domain_.dimension(1)};
    auto dim_num_{domain_.ndim()};

    auto max_rows_{
        (array_rows_.template domain<row_domain_type>().second -
         array_rows_.template domain<row_domain_type>().first + 1)};
    auto max_cols_{
        (array_cols_.template domain<col_domain_type>().second -
         array_cols_.template domain<col_domain_type>().first + 1)};

    if ((std::is_same_v<LayoutPolicy, Kokkos::layout_right> && cell_order == TILEDB_COL_MAJOR)
        || (std::is_same_v<LayoutPolicy, Kokkos::layout_left> && cell_order == TILEDB_ROW_MAJOR)) {
      std::swap(row_begin, col_begin);
      std::swap(row_end, col_end);
    }

    if (row_begin == 0 && row_end == 0) {
      row_end = max_rows_;
    }
    if (col_begin == 0 && col_end == 0) {
      col_end = max_cols_;
    }

    auto num_rows = row_end - row_begin;
    auto num_cols = col_end - col_begin;

    auto attr_num{schema_.attribute_num()};
    auto attr = schema_.attribute(idx);

    std::string attr_name = attr.name();
    tiledb_datatype_t attr_type = attr.type();

    // This should be set in initialization
    // ctx_.set_tag("vfs.s3.region", global_region.c_str());

    // Create a subarray that reads the array up to the specified subset.
    std::vector<int32_t> subarray_vals = {
        (int32_t)row_begin,
        (int32_t)row_end - 1,
        (int32_t)col_begin,
        (int32_t)col_end - 1};
    tiledb::Subarray subarray(ctx_, array_);
    subarray.set_subarray(subarray_vals);

//    auto layout_order = std::is_same_v<LayoutPolicy, Kokkos::layout_right> ? TILEDB_ROW_MAJOR : TILEDB_COL_MAJOR;
    auto layout_order = cell_order;

#ifndef __APPLE__
    auto data_ = std::make_unique_for_overwrite<T[]>(num_rows * num_cols);
#else
    // auto data_ = std::make_unique<T[]>(new T[mat_rows_ * mat_cols_]);
    auto data_ = std::unique_ptr<T[]>(new T[num_rows * num_cols]);
#endif

    tiledb::Query query(ctx_, array_);
    query.set_subarray(subarray)
        .set_layout(layout_order)
        .set_data_buffer(attr_name, data_.get(), num_rows * num_cols);

    query.submit();
    array_.close();
    assert(tiledb::Query::Status::COMPLETE == query.query_status());

    if ((std::is_same_v<LayoutPolicy, Kokkos::layout_right> && cell_order == TILEDB_COL_MAJOR)
        || (std::is_same_v<LayoutPolicy, Kokkos::layout_left> && cell_order == TILEDB_ROW_MAJOR)) {
      std::swap(num_rows, num_cols);
    }

    Base::operator=(Base{std::move(data_), num_rows, num_cols});
  }
};

/**
 * Convenience class for row-major matrices.
 */
template <class T, class I = size_t>
using tdbRowMajorMatrix = tdbMatrix<T, Kokkos::layout_right, I>;

/**
 * Convenience class for column-major matrices.
 */
template <class T, class I = size_t>
using tdbColMajorMatrix = tdbMatrix<T, Kokkos::layout_left, I>;

/**
 * Write the contents of a Matrix to a TileDB array.
 */
template <class T, class LayoutPolicy = Kokkos::layout_right, class I = size_t>
void write_matrix(const Matrix<T, LayoutPolicy, I>& A, const std::string& uri) {
  if (global_debug) {
    std::cerr << "# Writing Matrix: " << uri << std::endl;
  }

  life_timer _{"write matrix " + uri};

  // Create context
  std::map<std::string, std::string> init_{{"vfs.s3.region", global_region.c_str()}};
  tiledb::Config config_{init_};
  tiledb::Context ctx {config_};

  // @todo: make this a parameter
  size_t num_parts = 10;
  size_t row_extent =
      std::max<size_t>((A.num_rows() + num_parts - 1) / num_parts, A.num_rows() >= 2 ? 2 : 1);
  size_t col_extent =
      std::max<size_t>((A.num_cols() + num_parts - 1) / num_parts, A.num_cols() >= 2 ? 2 : 1);

  tiledb::Domain domain(ctx);
  domain
      .add_dimension(tiledb::Dimension::create<int>(
          ctx, "rows", {{0, (int)A.num_rows() - 1}}, row_extent))
      .add_dimension(tiledb::Dimension::create<int>(
          ctx, "cols", {{0, (int)A.num_cols() - 1}}, col_extent));

  // The array will be dense.
  tiledb::ArraySchema schema(ctx, TILEDB_DENSE);

  auto order = std::is_same_v<LayoutPolicy, Kokkos::layout_right> ? TILEDB_ROW_MAJOR : TILEDB_COL_MAJOR;
  schema.set_domain(domain).set_order({{order, order}});

  schema.add_attribute(tiledb::Attribute::create<T>(ctx, "values"));

  tiledb::Array::create(uri, schema);

  std::vector<int32_t> subarray_vals{
      0, (int)A.num_rows() - 1, 0, (int)A.num_cols() - 1};

  // Open array for writing
  tiledb::Array array(ctx, uri, TILEDB_WRITE);

  tiledb::Subarray subarray(ctx, array);
  subarray.set_subarray(subarray_vals);

  tiledb::Query query(ctx, array);

  query.set_layout(order)
      .set_data_buffer(
          "values", &A(0, 0), (int)A.num_rows() * (int)A.num_cols())
      .set_subarray(subarray);
  query.submit();

  array.close();
}

/**
 * Write the contents of a std::vector to a TileDB array.
 * @todo change the naming of this function to something more appropriate
 */
template <class T>
void write_vector(std::vector<T>& v, const std::string& uri) {
  if (global_debug) {
    std::cerr << "# Writing std::vector: " << uri << std::endl;
  }

  life_timer _{"write vector " + uri};

  // Create context
  std::map<std::string, std::string> init_{{"vfs.s3.region", global_region.c_str()}};
  tiledb::Config config_{init_};
  tiledb::Context ctx {config_};

  size_t num_parts = 10;
  size_t tile_extent = (size(v) + num_parts - 1) / num_parts;
  tiledb::Domain domain(ctx);
  domain.add_dimension(tiledb::Dimension::create<int>(
      ctx, "rows", {{0, (int)size(v) - 1}}, tile_extent));

  // The array will be dense.
  tiledb::ArraySchema schema(ctx, TILEDB_DENSE);
  schema.set_domain(domain).set_order({{TILEDB_ROW_MAJOR, TILEDB_ROW_MAJOR}});

  schema.add_attribute(tiledb::Attribute::create<T>(ctx, "values"));

  tiledb::Array::create(uri, schema);
  // Set the subarray to write into
  std::vector<int32_t> subarray_vals{0, (int)size(v) - 1};

  // Open array for writing
  tiledb::Array array(ctx, uri, TILEDB_WRITE);

  tiledb::Subarray subarray(ctx, array);
  subarray.set_subarray(subarray_vals);

  tiledb::Query query(ctx, array);
  query.set_layout(TILEDB_ROW_MAJOR)
      .set_data_buffer("values", v)
      .set_subarray(subarray);
  query.submit();

  array.close();
}

/**
 * Read the contents of a TileDB array into a std::vector.
 */
template <class T>
auto read_vector(const std::string& uri) {
  if (global_debug) {
    std::cerr << "# Reading std::vector: " << uri << std::endl;
  }

  auto init_ =
      std::map<std::string, std::string>{{"vfs.s3.region", global_region.c_str()}};
  auto config_ = tiledb::Config{init_};
  auto ctx_ = tiledb::Context{config_};

  auto array_ = tiledb::Array{ctx_, uri, TILEDB_READ};
  auto schema_ = array_.schema();

  life_timer _{"read vector " + uri};
  using domain_type = int32_t;
  const size_t idx = 0;

  auto domain_{schema_.domain()};

  auto dim_num_{domain_.ndim()};
  auto array_rows_{domain_.dimension(0)};

  auto vec_rows_{
      (array_rows_.template domain<domain_type>().second -
       array_rows_.template domain<domain_type>().first + 1)};

  auto attr_num{schema_.attribute_num()};
  auto attr = schema_.attribute(idx);

  std::string attr_name = attr.name();
  tiledb_datatype_t attr_type = attr.type();

  // Create a subarray that reads the array up to the specified subset.
  std::vector<int32_t> subarray_vals = {0, vec_rows_ - 1};
  tiledb::Subarray subarray(ctx_, array_);
  subarray.set_subarray(subarray_vals);

  // @todo: use something non-initializing
  std::vector<T> data_(vec_rows_);

  tiledb::Query query(ctx_, array_);
  query.set_subarray(subarray).set_data_buffer(
      attr_name, data_.data(), vec_rows_);

  query.submit();
  array_.close();
  assert(tiledb::Query::Status::COMPLETE == query.query_status());

  return data_;
}


/**
 * Is the matrix row-oriented?
 */
template <class Matrix>
constexpr bool is_row_oriented(const Matrix& A) {
  return std::is_same_v<typename Matrix::layout_policy, Kokkos::layout_right>;
}

/**
 * Print information about a Matrix.
 * @param A
 */
template <class Matrix>
std::string matrix_info(const Matrix& A, const std::string& msg = "") {
  std::string str = "# " + msg;
  if (!msg.empty()) {
    str += ": ";
  }
  str += "Shape: ( " + std::to_string(A.num_rows()) + ", " + std::to_string(A.num_cols()) + " )";
  str += std::string(" Layout: ") + (is_row_oriented(A) ? "row major" : "column major");
  return str;
}

/**
 * Print information about a std::vector -- overload.
 * @param A
 */
template <class T>
std::string matrix_info(const std::vector<T>& A, const std::string& msg = "") {
  std::string str = "# " + msg;
  if (!msg.empty()) {
    str += ": ";
  }
  str += "Shape: (" + std::to_string(A.size()) + " )";
  return str;
}

template <class Matrix>
void debug_matrix(const Matrix& A, const std::string& msg = "") {
  extern bool global_debug;
  if (global_debug) {
    std::cout << matrix_info(A, msg) << std::endl;
  }
}
#endif  // TDB_LINALG_H