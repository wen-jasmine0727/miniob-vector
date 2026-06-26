/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <cmath>
#include <cctype>
#include <string>
#include "common/value.h"

/**
 * @brief Vector utility functions including distance calculations
 * @ingroup DataType
 */

/**
 * @brief Calculates the L2 (Euclidean) distance between two vectors.
 *        D = sqrt(sum((Ai - Bi)^2))
 * @return The L2 distance, or -1 on error.
 */
inline float vector_l2_distance(const float *a, const float *b, int dim)
{
  if (a == nullptr || b == nullptr || dim <= 0) return -1.0f;
  float sum = 0.0f;
  for (int i = 0; i < dim; i++) {
    float diff = a[i] - b[i];
    sum += diff * diff;
  }
  return std::sqrt(sum);
}

/**
 * @brief Calculates the cosine distance between two vectors.
 *        D = 1 - (A·B)/(|A|*|B|)
 * @return The cosine distance in [0, 2], or -1 on error.
 */
inline float vector_cosine_distance(const float *a, const float *b, int dim)
{
  if (a == nullptr || b == nullptr || dim <= 0) return -1.0f;
  float dot = 0.0f;
  float norm_a = 0.0f;
  float norm_b = 0.0f;
  for (int i = 0; i < dim; i++) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }
  if (norm_a == 0.0f || norm_b == 0.0f) return -1.0f;
  float cosine_sim = dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
  // Clamp to [-1, 1] to handle floating point errors
  if (cosine_sim > 1.0f) cosine_sim = 1.0f;
  if (cosine_sim < -1.0f) cosine_sim = -1.0f;
  return 1.0f - cosine_sim;
}

/**
 * @brief Calculates the inner product (dot product) between two vectors.
 *        D = sum(Ai * Bi)
 * @return The dot product, or a very negative value on error.
 */
inline float vector_inner_product(const float *a, const float *b, int dim)
{
  if (a == nullptr || b == nullptr || dim <= 0) return -std::numeric_limits<float>::max();
  float sum = 0.0f;
  for (int i = 0; i < dim; i++) {
    sum += a[i] * b[i];
  }
  return sum;
}

/**
 * @brief Calculate distance between two vectors using the specified method.
 * @param a First vector data
 * @param b Second vector data
 * @param dim Dimension of both vectors
 * @param method Distance method: "COSINE", "DOT", or "EUCLIDEAN"
 * @param[out] result The computed distance
 * @return RC::SUCCESS or RC::INVALID_ARGUMENT
 */
inline RC vector_distance(const float *a, const float *b, int dim, const std::string &method, float &result)
{
  if (a == nullptr || b == nullptr || dim <= 0) {
    return RC::INVALID_ARGUMENT;
  }

  // 转大写实现大小写不敏感
  string upper_method = method;
  for (char &c : upper_method) c = toupper(c);

  if (upper_method == "EUCLIDEAN" || upper_method == "L2" || upper_method == "L2_DISTANCE") {
    result = vector_l2_distance(a, b, dim);
  } else if (upper_method == "COSINE" || upper_method == "COSINE_DISTANCE") {
    result = vector_cosine_distance(a, b, dim);
  } else if (upper_method == "DOT" || upper_method == "INNER_PRODUCT" || upper_method == "INNER_PRODUCT_DISTANCE") {
    result = vector_inner_product(a, b, dim);
  } else {
    return RC::INVALID_ARGUMENT;
  }
  return RC::SUCCESS;
}

/**
 * @brief Calculate distance between two Value vectors.
 */
inline RC vector_distance(const Value &left, const Value &right, const std::string &method, Value &result)
{
  if (left.attr_type() != AttrType::VECTORS || right.attr_type() != AttrType::VECTORS) {
    return RC::INVALID_ARGUMENT;
  }
  int ld = left.vector_dim();
  int rd = right.vector_dim();
  if (ld != rd) {
    return RC::INVALID_ARGUMENT;
  }
  float dist;
  RC    rc = vector_distance(left.get_vector_data(), right.get_vector_data(), ld, method, dist);
  if (rc != RC::SUCCESS) return rc;
  result.set_float(dist);
  return RC::SUCCESS;
}
