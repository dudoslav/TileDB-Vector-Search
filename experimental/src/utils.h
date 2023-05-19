/**
 * @file   utils.h
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
 *
 */

#ifndef TDB_UTILS_H
#define TDB_UTILS_H

#include <regex>
#include <string>
#include <filesystem>


bool is_http_address(const std::string& filename) {
  std::regex httpRegex("^https?://.*");
  return std::regex_match(filename, httpRegex);
}

bool is_s3_container(const std::string& filename) {
  std::regex awsRegex("^[a-zA-Z0-9]+\\.s3\\.amazonaws\\.com.*");
  std::regex s3Regex("^s3://.*");
  return std::regex_match(filename, s3Regex) ||
         std::regex_match(filename, s3Regex);
}

std::string get_filename(const std::string& filename) {
  std::regex fileRegex("^file://(.*)");
  std::smatch match;

  if (std::regex_match(filename, match, fileRegex) && match.size() > 1) {
    std::string extractedString = match.str(1);
    return extractedString;
  }
  return filename;
}

bool local_directory_exists(const std::string& path) {
  std::filesystem::path directoryPath(path);
  auto a = std::filesystem::status(directoryPath);
  auto b = std::filesystem::is_directory(directoryPath);

  return std::filesystem::is_directory(directoryPath);
}

bool is_local_directory(const std::string& path) {
  return local_directory_exists(path);
}

bool subdirectory_exists(const std::string& path, const std::string& subdirectoryName) {
  std::filesystem::path directoryPath(path);
  std::filesystem::path subdirectoryPath = directoryPath / subdirectoryName;

  return std::filesystem::is_directory(subdirectoryPath);
}

bool local_file_exists(const std::string& filename) {
  if (is_http_address(filename) || is_s3_container(filename) || is_local_directory(filename)){
    return false;
  }
  auto fname = get_filename(filename);
  std::filesystem::path filePath(fname);
  return std::filesystem::is_regular_file(filePath);
}

bool is_local_file(const std::string& filename) {
  return local_file_exists(filename);
}

bool local_array_exists(const std::string& array_uri) {
  auto aname = get_filename(array_uri);
  return local_directory_exists(aname) && subdirectory_exists(array_uri, "__schema");
}

bool is_local_array(const std::string& array_uri) {
  return local_array_exists(array_uri);
}


#endif
