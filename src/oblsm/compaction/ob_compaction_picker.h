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

#include "oblsm/table/ob_sstable.h"
#include "common/sys/rc.h"
#include "oblsm/compaction/ob_compaction.h"
#include "oblsm/util/ob_comparator.h"

namespace oceanbase {

/**
 * @class ObCompactionPicker
 * @brief Abstract base class for compaction picker strategies in an LSM-Tree.
 *
 * This class defines the interface for picking a compaction task from a set of SSTables.
 * Derived classes will implement specific compaction strategies (e.g., tiered or leveled compaction).
 */
class ObCompactionPicker
{
public:
  /**
   * @param options Pointer to the LSM-Tree options configuration.
   */
  ObCompactionPicker(ObLsmOptions *options) : options_(options) {}

  virtual ~ObCompactionPicker() = default;

  /**
   * @brief Pure virtual method to pick a compaction task.
   * @param sstables A pointer to the SSTables available for compaction.
   * @return A unique pointer to the selected compaction task.
   */
  virtual unique_ptr<ObCompaction> pick(SSTablesPtr sstables) = 0;

  /**
   * @brief Static factory method to create a specific compaction picker.
   * @param type The type of compaction strategy (e.g., tiered, leveled).
   * @param options Pointer to the LSM-Tree options configuration.
   * @return A pointer to the created ObCompactionPicker instance.
   */
  static ObCompactionPicker *create(CompactionType type, ObLsmOptions *options);

protected:
  ObLsmOptions *options_;  ///< Pointer to the LSM-Tree options configuration.
};

/**
 * @class TiredCompactionPicker
 * @brief A class implementing the tiered compaction strategy.
 *
 */
class TiredCompactionPicker : public ObCompactionPicker
{
public:
  /**
   * @param options Pointer to the LSM-Tree options configuration.
   */
  TiredCompactionPicker(ObLsmOptions *options) : ObCompactionPicker(options) {}

  ~TiredCompactionPicker() = default;

  /**
   * @brief Implementation of the pick method for tiered compaction.
   */
  unique_ptr<ObCompaction> pick(SSTablesPtr sstables) override;

private:
};

/**
 * @class LeveledCompactionPicker
 * @brief A class implementing the leveled compaction strategy.
 *
 * In leveled compaction, each level (except L0) contains a single sorted run
 * (SSTables with non-overlapping key ranges). L0 is limited by file count.
 * When a level exceeds its size/ file count threshold, a compaction is triggered
 * that merges SSTables from that level with overlapping SSTables from the next level.
 */
class LeveledCompactionPicker : public ObCompactionPicker
{
public:
  LeveledCompactionPicker(ObLsmOptions *options) : ObCompactionPicker(options) {}

  ~LeveledCompactionPicker() = default;

  unique_ptr<ObCompaction> pick(SSTablesPtr sstables) override;

private:
  /**
   * @brief Calculate the score of a level for compaction priority.
   * A score > 1 means the level needs compaction.
   */
  double calc_level_score(SSTablesPtr sstables, int level);

  /**
   * @brief Check if two SSTables have overlapping key ranges.
   */
  bool key_range_overlap(const shared_ptr<ObSSTable> &a, const shared_ptr<ObSSTable> &b);
};

}  // namespace oceanbase