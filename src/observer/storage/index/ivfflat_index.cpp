/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "storage/index/ivfflat_index.h"

#include <cmath>

RC IvfflatIndex::create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  RC rc = init(index_meta, field_meta);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  table_         = table;
  file_name_     = file_name == nullptr ? "" : file_name;
  distance_type_ = index_meta.distance_type();
  lists_         = std::max(1, index_meta.lists());
  probes_        = std::max(1, std::min(index_meta.probes(), lists_));
  dim_           = field_meta.len() / static_cast<int>(sizeof(float));
  centroids_.clear();
  buckets_.assign(lists_, {});
  inited_ = true;
  return sync();
}

RC IvfflatIndex::open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  RC rc = init(index_meta, field_meta);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  table_         = table;
  file_name_     = file_name == nullptr ? "" : file_name;
  distance_type_ = index_meta.distance_type();
  lists_         = std::max(1, index_meta.lists());
  probes_        = std::max(1, std::min(index_meta.probes(), lists_));
  dim_           = field_meta.len() / static_cast<int>(sizeof(float));
  buckets_.assign(lists_, {});
  inited_ = true;
  return load();
}

RC IvfflatIndex::insert_entry(const char *record, const RID *rid)
{
  if (!inited_ || record == nullptr || rid == nullptr || dim_ <= 0) {
    return RC::INVALID_ARGUMENT;
  }

  const float *raw = reinterpret_cast<const float *>(record + field_meta_.offset());
  vector<float> v(raw, raw + dim_);
  if (centroids_.empty()) {
    centroids_.push_back(v);
    buckets_.assign(lists_, {});
  }

  int bucket = nearest_centroid(v);
  if (bucket < 0 || bucket >= static_cast<int>(buckets_.size())) {
    bucket = 0;
  }
  buckets_[bucket].push_back(Entry{*rid, std::move(v)});
  return RC::SUCCESS;
}

RC IvfflatIndex::delete_entry(const char *record, const RID *rid)
{
  if (!inited_ || rid == nullptr) {
    return RC::INVALID_ARGUMENT;
  }

  for (auto &bucket : buckets_) {
    auto it = std::remove_if(bucket.begin(), bucket.end(), [rid](const Entry &entry) { return entry.rid == *rid; });
    if (it != bucket.end()) {
      bucket.erase(it, bucket.end());
      return RC::SUCCESS;
    }
  }
  return RC::SUCCESS;
}

int IvfflatIndex::nearest_centroid(const vector<float> &v) const
{
  if (centroids_.empty()) {
    return 0;
  }

  int   best = 0;
  float best_dist = std::numeric_limits<float>::max();
  for (int i = 0; i < static_cast<int>(centroids_.size()); i++) {
    float dist = distance(v, centroids_[i]);
    if (dist < best_dist) {
      best = i;
      best_dist = dist;
    }
  }
  return best;
}

void IvfflatIndex::rebuild_centroids()
{
  vector<Entry> all_entries;
  for (const auto &bucket : buckets_) {
    all_entries.insert(all_entries.end(), bucket.begin(), bucket.end());
  }
  buckets_.assign(lists_, {});
  centroids_.clear();

  if (all_entries.empty()) {
    return;
  }

  int centroid_count = std::min(lists_, static_cast<int>(all_entries.size()));
  centroids_.reserve(centroid_count);
  for (int i = 0; i < centroid_count; i++) {
    int pos = static_cast<int>((static_cast<int64_t>(i) * all_entries.size()) / centroid_count);
    centroids_.push_back(all_entries[pos].embedding);
  }

  for (int iter = 0; iter < 8; iter++) {
    vector<vector<Entry>> new_buckets(lists_);
    for (const Entry &entry : all_entries) {
      int bucket = nearest_centroid(entry.embedding);
      new_buckets[bucket].push_back(entry);
    }

    for (int i = 0; i < static_cast<int>(centroids_.size()); i++) {
      if (new_buckets[i].empty()) {
        continue;
      }
      vector<float> mean(dim_, 0.0f);
      for (const Entry &entry : new_buckets[i]) {
        for (int d = 0; d < dim_; d++) {
          mean[d] += entry.embedding[d];
        }
      }
      for (float &x : mean) {
        x /= static_cast<float>(new_buckets[i].size());
      }
      centroids_[i] = std::move(mean);
    }
    buckets_ = std::move(new_buckets);
  }
}

vector<RID> IvfflatIndex::ann_search(const vector<float> &base_vector, size_t limit)
{
  vector<RID> result;
  if (!inited_ || base_vector.size() != static_cast<size_t>(dim_) || limit == 0) {
    return result;
  }

  vector<pair<float, int>> centroid_distances;
  for (int i = 0; i < static_cast<int>(centroids_.size()); i++) {
    centroid_distances.emplace_back(distance(base_vector, centroids_[i]), i);
  }
  std::sort(centroid_distances.begin(), centroid_distances.end());

  vector<pair<float, RID>> candidates;
  int probe_count = std::min(probes_, static_cast<int>(centroid_distances.size()));
  for (int i = 0; i < probe_count; i++) {
    int bucket_id = centroid_distances[i].second;
    if (bucket_id < 0 || bucket_id >= static_cast<int>(buckets_.size())) {
      continue;
    }
    for (const Entry &entry : buckets_[bucket_id]) {
      candidates.emplace_back(distance(base_vector, entry.embedding), entry.rid);
    }
  }

  std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
    if (a.first != b.first) {
      return a.first < b.first;
    }
    return RID::compare(&a.second, &b.second) < 0;
  });

  size_t result_count = std::min(limit, candidates.size());
  result.reserve(result_count);
  for (size_t i = 0; i < result_count; i++) {
    result.push_back(candidates[i].second);
  }
  return result;
}

RC IvfflatIndex::sync()
{
  if (file_name_.empty()) {
    return RC::SUCCESS;
  }

  std::ofstream out(file_name_, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return RC::IOERR_OPEN;
  }

  const char magic[8] = {'I', 'V', 'F', 'F', 'L', 'A', 'T', '1'};
  out.write(magic, sizeof(magic));
  int32_t lists = lists_, probes = probes_, dim = dim_;
  int32_t centroid_count = static_cast<int32_t>(centroids_.size());
  out.write(reinterpret_cast<const char *>(&lists), sizeof(lists));
  out.write(reinterpret_cast<const char *>(&probes), sizeof(probes));
  out.write(reinterpret_cast<const char *>(&dim), sizeof(dim));
  out.write(reinterpret_cast<const char *>(&centroid_count), sizeof(centroid_count));
  for (const auto &centroid : centroids_) {
    out.write(reinterpret_cast<const char *>(centroid.data()), sizeof(float) * centroid.size());
  }

  int32_t bucket_count = static_cast<int32_t>(buckets_.size());
  out.write(reinterpret_cast<const char *>(&bucket_count), sizeof(bucket_count));
  for (const auto &bucket : buckets_) {
    int32_t entry_count = static_cast<int32_t>(bucket.size());
    out.write(reinterpret_cast<const char *>(&entry_count), sizeof(entry_count));
    for (const Entry &entry : bucket) {
      out.write(reinterpret_cast<const char *>(&entry.rid.page_num), sizeof(entry.rid.page_num));
      out.write(reinterpret_cast<const char *>(&entry.rid.slot_num), sizeof(entry.rid.slot_num));
      out.write(reinterpret_cast<const char *>(entry.embedding.data()), sizeof(float) * entry.embedding.size());
    }
  }

  return out.good() ? RC::SUCCESS : RC::IOERR_WRITE;
}

RC IvfflatIndex::load()
{
  std::ifstream in(file_name_, std::ios::binary);
  if (!in.is_open()) {
    return RC::SUCCESS;
  }

  char magic[8] = {};
  in.read(magic, sizeof(magic));
  if (std::string(magic, sizeof(magic)) != "IVFFLAT1") {
    return RC::INTERNAL;
  }

  int32_t lists = 0, probes = 0, dim = 0, centroid_count = 0;
  in.read(reinterpret_cast<char *>(&lists), sizeof(lists));
  in.read(reinterpret_cast<char *>(&probes), sizeof(probes));
  in.read(reinterpret_cast<char *>(&dim), sizeof(dim));
  in.read(reinterpret_cast<char *>(&centroid_count), sizeof(centroid_count));
  if (!in.good() || lists <= 0 || dim != dim_) {
    return RC::INTERNAL;
  }

  lists_  = lists;
  probes_ = std::max(1, std::min(probes, lists_));
  centroids_.assign(centroid_count, vector<float>(dim_));
  for (auto &centroid : centroids_) {
    in.read(reinterpret_cast<char *>(centroid.data()), sizeof(float) * centroid.size());
  }

  int32_t bucket_count = 0;
  in.read(reinterpret_cast<char *>(&bucket_count), sizeof(bucket_count));
  buckets_.assign(std::max(1, bucket_count), {});
  for (int i = 0; i < bucket_count; i++) {
    int32_t entry_count = 0;
    in.read(reinterpret_cast<char *>(&entry_count), sizeof(entry_count));
    buckets_[i].reserve(entry_count);
    for (int j = 0; j < entry_count; j++) {
      Entry entry;
      entry.embedding.resize(dim_);
      in.read(reinterpret_cast<char *>(&entry.rid.page_num), sizeof(entry.rid.page_num));
      in.read(reinterpret_cast<char *>(&entry.rid.slot_num), sizeof(entry.rid.slot_num));
      in.read(reinterpret_cast<char *>(entry.embedding.data()), sizeof(float) * entry.embedding.size());
      buckets_[i].push_back(std::move(entry));
    }
  }

  return in.good() || in.eof() ? RC::SUCCESS : RC::IOERR_READ;
}
