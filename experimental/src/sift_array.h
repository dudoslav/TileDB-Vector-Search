/**
 * @file   sift_array.h
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

#ifndef TDB_SIFT_ARRAY_H
#define TDB_SIFT_ARRAY_H

#include <cassert>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <tiledb/tiledb>

/**
 * Class for reading SIFT data stored in a TileDB array.  This was based off the
 * sift_db class, which handled the specific format from
 * http://corpus-texmex.irisa.fr . However, we ingested the original SIFT data
 * into a TileDB array, which has no SIFT-specific characteristics -- it's just
 * a 2D array of floats (or chars).
 *
 * The class reads the TileDB array and provides a std::vector<std::span<T>>
 * interface, which is basically a column-oriented view of the data. Much better
 * would be to use `mdspan`.
 *
 * @todo: Switch to using `mdspan` instead of `vector<span>` for better
 * performance.
 *
 * @tparam T The type of the data in the array (float or char for ANN_SIFT).
 */
template <class T>
class sift_array : public std::vector<std::span<T>> {
  // @todo: Switch to using `mdspan` instead of `vector<span>` for better
  // performance.
  using Base = std::vector<std::span<T>>;

  std::map<std::string, std::string> init_{{"vfs.s3.region", "us-west-2"}};
  tiledb::Config config_{init_};
  tiledb::Context ctx_{config_};

  tiledb::Array array_;
  tiledb::ArraySchema schema_;
  tiledb::Domain domain_;
  tiledb::Dimension rows_;
  tiledb::Dimension cols_;
  uint32_t dim_num_;
  int32_t num_rows_;
  int32_t num_cols_;

  std::unique_ptr<T[]> data_;

 public:
  /**
   * Constructor for reading a TileDB array.
   * @param array_name URI of the TileDB array to read.
   * @param subset How many of the columns to read.  If 0, read all columns.
   */
  sift_array(const std::string& array_name, size_t subset = 0)
      : array_(ctx_, array_name, TILEDB_READ)
      , schema_(array_.schema())
      , domain_(schema_.domain())
      , rows_(domain_.dimension("rows"))
      , cols_(domain_.dimension("cols"))
      , dim_num_(domain_.ndim())
      , num_rows_(
            rows_.domain<int32_t>().second - rows_.domain<int32_t>().first + 1)
      , num_cols_(
            subset == 0 ? (cols_.domain<int32_t>().second -
                           cols_.domain<int32_t>().first + 1) :
                          subset)
#ifndef __APPLE__
      , data_ {
    std::make_unique_for_overwrite<T[]>(num_rows_ * num_cols_)
  }
#else
      //, data_ {std::make_unique<T[]>(new T[num_rows_ * num_cols_])}
      , data_{new T[num_rows_ * num_cols_]}
#endif
  {
    ctx_.set_tag("vfs.s3.region", "us-west-2");

    // Initialize the view of the data.  Replace with `mdspan`
    this->resize(num_cols_);
    for (decltype(num_cols_) j = 0; j < num_cols_; ++j) {
      Base::operator[](j) =
          std::span<T>(data_.get() + j * num_rows_, num_rows_);
    }

    // Create a subarray that reads the array up to the specified subset.
    std::vector<int> subarray_vals = {0, num_rows_ - 1, 0, num_cols_ - 1};
    tiledb::Subarray subarray(ctx_, array_);
    subarray.set_subarray(subarray_vals);

    // Allocate query and set subarray.
    tiledb::Query query(ctx_, array_);
    query.set_subarray(subarray)
        .set_layout(TILEDB_COL_MAJOR)
        .set_data_buffer("a", data_.get(), num_rows_ * num_cols_);
    // Read from the array.
    query.submit();
    array_.close();
    assert(tiledb::Query::Status::COMPLETE == query.query_status());
  }
  auto& operator()(size_t i, size_t j) {
    return Base::operator[](j)[i];
  }
  auto operator()(size_t i, size_t j) const {
    return Base::operator[](j)[i];
  }
};

#endif  // TDB_SIFT_ARRAY_H