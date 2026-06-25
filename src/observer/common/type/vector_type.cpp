/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/type/vector_type.h"
#include "common/value.h"

#include <cmath>
#include <sstream>

using namespace std;

int VectorType::compare(const Value &left, const Value &right) const
{
  // Vectors can only be compared for equality
  const float *ld = left.get_vector_data();
  const float *rd = right.get_vector_data();
  if (ld == nullptr && rd == nullptr) return 0;
  if (ld == nullptr) return -1;
  if (rd == nullptr) return 1;
  int ld_dim = left.vector_dim();
  int rd_dim = right.vector_dim();
  if (ld_dim != rd_dim) {
    return (ld_dim < rd_dim) ? -1 : 1;
  }
  for (int i = 0; i < ld_dim; i++) {
    if (fabs(ld[i] - rd[i]) > 1e-6f) {
      return (ld[i] < rd[i]) ? -1 : 1;
    }
  }
  return 0;
}

RC VectorType::to_string(const Value &val, string &result) const
{
  const float *data = val.get_vector_data();
  int          dim  = val.vector_dim();
  if (data == nullptr || dim == 0) {
    result = "[]";
    return RC::SUCCESS;
  }
  stringstream ss;
  ss << "[";
  for (int i = 0; i < dim; i++) {
    if (i > 0) ss << ",";
    ss << data[i];
  }
  ss << "]";
  result = ss.str();
  return RC::SUCCESS;
}

RC VectorType::set_value_from_str(Value &val, const string &data) const
{
  val.set_type(AttrType::VECTORS);
  val.set_vector_from_str(data);
  return RC::SUCCESS;
}

int VectorType::cast_cost(AttrType type)
{
  if (type == attr_type_) return 0;
  if (type == AttrType::CHARS) return 1;
  return INT32_MAX;
}

RC VectorType::cast_to(const Value &val, AttrType type, Value &result) const
{
  if (type == attr_type_) {
    result = val;
    return RC::SUCCESS;
  }
  if (type == AttrType::CHARS) {
    string s;
    RC     rc = to_string(val, s);
    if (rc != RC::SUCCESS) return rc;
    result.set_string(s.c_str());
    return RC::SUCCESS;
  }
  return RC::UNSUPPORTED;
}