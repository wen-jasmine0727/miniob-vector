/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "oblsm/compaction/ob_compaction_picker.h"
#include "common/log/log.h"

namespace oceanbase {

// TODO: put it in options
unique_ptr<ObCompaction> TiredCompactionPicker::pick(SSTablesPtr sstables)
{
  if (sstables->size() < options_->default_run_num) {
    return nullptr;
  }
  unique_ptr<ObCompaction> compaction(new ObCompaction(0));
  // TODO(opt): a tricky compaction picker, just pick all sstables if enough sstables.
  for (size_t i = 0; i < sstables->size(); ++i) {
    size_t tire_i_size = (*sstables)[i].size();
    for (size_t j = 0; j < tire_i_size; ++j) {
      compaction->inputs_[0].emplace_back((*sstables)[i][j]);
    }
  }
  // TODO: LOG_DEBUG for debug
  return compaction;
}

ObCompactionPicker *ObCompactionPicker::create(CompactionType type, ObLsmOptions *options)
{

  switch (type) {
    case CompactionType::TIRED: return new TiredCompactionPicker(options);
    case CompactionType::LEVELED: return new LeveledCompactionPicker(options);
    default: return nullptr;
  }
  return nullptr;
}

double LeveledCompactionPicker::calc_level_score(SSTablesPtr sstables, int level)
{
  if (level == 0) {
    // L0 score is based on file count
    if (options_->default_l0_file_num == 0) {
      return 0.0;
    }
    return static_cast<double>(sstables->at(0).size()) / options_->default_l0_file_num;
  }

  // For L1 and above, score is total size / target size
  uint64_t target_size = options_->default_l1_level_size;
  for (int i = 1; i < level; i++) {
    target_size *= options_->default_level_ratio;
  }

  uint64_t level_size = 0;
  for (const auto &sst : (*sstables)[level]) {
    level_size += sst->size();
  }

  if (target_size == 0) {
    return 0.0;
  }
  return static_cast<double>(level_size) / target_size;
}

bool LeveledCompactionPicker::key_range_overlap(const shared_ptr<ObSSTable> &a, const shared_ptr<ObSSTable> &b)
{
  // Two SSTables overlap if neither is entirely before the other.
  // Since SSTables are sorted, a is before b if a.last_key < b.first_key.
  // We use user keys for comparison (strip sequence number).
  // For simplicity, compare the raw first/last keys using string comparison.
  // The keys already include internal key format but for range checks,
  // lexicographic comparison is sufficient since user keys are prefixes.
  if (a->last_key() < b->first_key() || b->last_key() < a->first_key()) {
    return false;
  }
  return true;
}

unique_ptr<ObCompaction> LeveledCompactionPicker::pick(SSTablesPtr sstables)
{
  if (sstables->empty()) {
    return nullptr;
  }

  int levels = static_cast<int>(sstables->size());

  // Calculate scores and find the level with highest score > 1.0
  int    best_level = -1;
  double best_score = 1.0;  // Only pick if score > 1.0

  for (int i = 0; i < levels - 1; i++) {  // Don't compact from the last level
    double score = calc_level_score(sstables, i);
    if (score > best_score) {
      best_score = score;
      best_level = i;
    }
  }

  if (best_level < 0) {
    return nullptr;
  }

  unique_ptr<ObCompaction> compaction(new ObCompaction(best_level));

  if (best_level == 0) {
    // L0 -> L1: pick ALL L0 files + ALL L1 files with overlapping key ranges
    for (const auto &sst : (*sstables)[0]) {
      compaction->inputs_[0].push_back(sst);
    }

    if (levels > 1) {
      for (const auto &sst : (*sstables)[1]) {
        bool overlap = false;
        for (const auto &l0_sst : (*sstables)[0]) {
          if (key_range_overlap(l0_sst, sst)) {
            overlap = true;
            break;
          }
        }
        if (overlap) {
          compaction->inputs_[1].push_back(sst);
        }
      }
    }
  } else {
    // L_i -> L_{i+1}: pick the SSTable at L_i that was least recently compacted
    // (for simplicity, pick the last one in the vector)
    // + ALL L_{i+1} SSTables with overlapping key ranges
    if (!(*sstables)[best_level].empty()) {
      // Pick the SSTable at the back (least recently compacted / newest in the level)
      auto picked_sst = (*sstables)[best_level].back();
      compaction->inputs_[0].push_back(picked_sst);

      int next_level = best_level + 1;
      if (next_level < levels) {
        for (const auto &sst : (*sstables)[next_level]) {
          if (key_range_overlap(picked_sst, sst)) {
            compaction->inputs_[1].push_back(sst);
          }
        }
      }
    }
  }

  return compaction;
}

}  // namespace oceanbase