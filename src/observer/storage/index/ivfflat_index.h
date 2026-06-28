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

#include <algorithm>
#include <cstdint>
#include <limits>

#include "common/lang/fstream.h"
#include "common/lang/string.h"
#include "common/lang/utility.h"
#include "common/log/log.h"
#include "common/type/vector_util.h"
#include "storage/index/index.h"

/**
 * @brief ivfflat 向量索引
 * @ingroup Index
 */
class IvfflatIndex : public Index
{
public:
  IvfflatIndex(){};
  virtual ~IvfflatIndex() noexcept {};

  RC create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta) override;
  RC open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta) override;

  bool is_vector_index() override { return true; }

  vector<RID> ann_search(const vector<float> &base_vector, size_t limit);
  void rebuild() { rebuild_centroids(); }

  RC close() { return sync(); }

  RC insert_entry(const char *record, const RID *rid) override;
  RC delete_entry(const char *record, const RID *rid) override;

  IndexScanner *create_scanner(const char *, int, bool, const char *, int, bool) override { return nullptr; }

  RC sync() override;

private:
  struct Entry
  {
    RID           rid;
    vector<float> embedding;
  };

  float distance(const vector<float> &a, const vector<float> &b) const
  {
    float result = std::numeric_limits<float>::max();
    vector_distance(a.data(), b.data(), static_cast<int>(a.size()), distance_type_, result);
    return result;
  }

  int nearest_centroid(const vector<float> &v) const;
  void rebuild_centroids();
  RC   load();

private:
  bool                  inited_ = false;
  Table                *table_  = nullptr;
  string                file_name_;
  string                distance_type_ = "L2_DISTANCE";
  int                   lists_  = 1;
  int                   probes_ = 1;
  int                   dim_    = 0;
  vector<vector<float>> centroids_;
  vector<vector<Entry>> buckets_;
};
