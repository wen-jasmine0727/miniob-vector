/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "oblsm/table/ob_sstable.h"
#include "oblsm/util/ob_coding.h"
#include "common/log/log.h"
#include "common/lang/filesystem.h"
namespace oceanbase {

void ObSSTable::init()
{
  file_reader_ = ObFileReader::create_file_reader(file_name_);
  if (file_reader_ == nullptr) {
    LOG_ERROR("Failed to create file reader for SSTable %s", file_name_.c_str());
    return;
  }

  uint32_t file_size = file_reader_->file_size();
  if (file_size < sizeof(uint32_t)) {
    // Empty or invalid file
    return;
  }

  // Read the footer: last 4 bytes = meta_start_pos (offset where meta_count is written)
  string   footer_data = file_reader_->read_pos(file_size - sizeof(uint32_t), sizeof(uint32_t));
  uint32_t meta_start_pos = get_numeric<uint32_t>(footer_data.c_str());
  if (meta_start_pos >= file_size) {
    LOG_ERROR("Invalid meta_start_pos in SSTable %s", file_name_.c_str());
    return;
  }

  // Read meta_count at meta_start_pos
  string   meta_count_data = file_reader_->read_pos(meta_start_pos, sizeof(uint32_t));
  uint32_t meta_count = get_numeric<uint32_t>(meta_count_data.c_str());

  // Read each BlockMeta
  uint32_t read_pos_offset = meta_start_pos + sizeof(uint32_t);
  for (uint32_t i = 0; i < meta_count; i++) {
    // Read meta_size (4 bytes)
    string   meta_size_data = file_reader_->read_pos(read_pos_offset, sizeof(uint32_t));
    uint32_t meta_size      = get_numeric<uint32_t>(meta_size_data.c_str());
    read_pos_offset += sizeof(uint32_t);

    // Read meta_data
    string     meta_data = file_reader_->read_pos(read_pos_offset, meta_size);
    BlockMeta  meta;
    meta.decode(meta_data);
    block_metas_.push_back(meta);
    read_pos_offset += meta_size;
  }
}

shared_ptr<ObBlock> ObSSTable::read_block_with_cache(uint32_t block_idx) const
{
  if (block_idx >= block_metas_.size()) {
    return nullptr;
  }
  if (block_cache_ == nullptr) {
    return read_block(block_idx);
  }

  uint64_t           cache_key = (static_cast<uint64_t>(sst_id_) << 32) | block_idx;
  shared_ptr<ObBlock> block;
  if (block_cache_->get(cache_key, block)) {
    return block;
  }

  block = read_block(block_idx);
  if (block != nullptr) {
    block_cache_->put(cache_key, block);
  }
  return block;
}

shared_ptr<ObBlock> ObSSTable::read_block(uint32_t block_idx) const
{
  if (block_idx >= block_metas_.size()) {
    return nullptr;
  }

  const BlockMeta &meta = block_metas_[block_idx];
  string           raw_data = file_reader_->read_pos(meta.offset_, meta.size_);
  if (raw_data.empty() && meta.size_ > 0) {
    LOG_ERROR("Failed to read block %u from SSTable %s", block_idx, file_name_.c_str());
    return nullptr;
  }

  auto block = make_shared<ObBlock>(comparator_);
  RC   rc    = block->decode(raw_data);
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Failed to decode block %u from SSTable %s, rc=%s", block_idx, file_name_.c_str(), strrc(rc));
    return nullptr;
  }
  return block;
}

void ObSSTable::remove() { filesystem::remove(file_name_); }

ObLsmIterator *ObSSTable::new_iterator() { return new TableIterator(get_shared_ptr()); }

void TableIterator::read_block_with_cache()
{
  block_ = sst_->read_block_with_cache(curr_block_idx_);
  block_iterator_.reset(block_->new_iterator());
}

void TableIterator::seek_to_first()
{
  curr_block_idx_ = 0;
  read_block_with_cache();
  block_iterator_->seek_to_first();
}

void TableIterator::seek_to_last()
{
  curr_block_idx_ = block_cnt_ - 1;
  read_block_with_cache();
  block_iterator_->seek_to_last();
}

void TableIterator::next()
{
  block_iterator_->next();
  if (block_iterator_->valid()) {
  } else if (curr_block_idx_ < block_cnt_ - 1) {
    curr_block_idx_++;
    read_block_with_cache();
    block_iterator_->seek_to_first();
  }
}

void TableIterator::seek(const string_view &lookup_key)
{
  curr_block_idx_ = 0;
  // TODO: use binary search
  for (; curr_block_idx_ < block_cnt_; curr_block_idx_++) {
    const auto &block_meta = sst_->block_meta(curr_block_idx_);
    if (sst_->comparator()->compare(extract_user_key(block_meta.last_key_), extract_user_key_from_lookup_key(lookup_key)) >= 0) {
      break;
    }
  }
  if (curr_block_idx_ == block_cnt_) {
    block_iterator_ = nullptr;
    return;
  }
  read_block_with_cache();
  block_iterator_->seek(lookup_key);
};

}  // namespace oceanbase
