/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "oblsm/table/ob_sstable_builder.h"
#include "oblsm/util/ob_coding.h"
#include "common/log/log.h"
#include "common/lang/filesystem.h"

namespace oceanbase {

// TODO: refactor build with mem_table/iterator logic.
RC ObSSTableBuilder::build(shared_ptr<ObMemTable> mem_table, const std::string &file_name, uint32_t sst_id)
{
  reset();
  sst_id_ = sst_id;

  // Create and open file writer
  file_writer_ = ObFileWriter::create_file_writer(file_name, false);
  if (file_writer_ == nullptr) {
    LOG_ERROR("Failed to create file writer for %s", file_name.c_str());
    return RC::IOERR_OPEN;
  }

  // Iterate through memtable and build blocks
  unique_ptr<ObLsmIterator> iter(mem_table->new_iterator());
  iter->seek_to_first();

  bool first_entry = true;
  while (iter->valid()) {
    if (first_entry) {
      curr_blk_first_key_ = string(iter->key());
      first_entry         = false;
    }

    RC add_rc = block_builder_.add(iter->key(), iter->value());
    if (add_rc == RC::FULL) {
      // Finish current block and start a new one
      finish_build_block();
      curr_blk_first_key_ = string(iter->key());
      // Re-add the entry that caused the block to be full
      block_builder_.add(iter->key(), iter->value());
    }
    iter->next();
  }

  // Flush the last block if it has data
  if (block_builder_.appro_size() > 0) {
    finish_build_block();
  }

  // Write block meta index at the end of the file
  // Format: meta_count (4B) | for each meta: meta_size (4B) + meta_data | footer: meta_start_pos (4B)
  uint32_t meta_start_pos = curr_offset_;

  // meta_count
  string meta_buf;
  put_numeric<uint32_t>(&meta_buf, block_metas_.size());

  // Each meta: size + data
  for (const auto &meta : block_metas_) {
    string encoded_meta = meta.encode();
    put_numeric<uint32_t>(&meta_buf, encoded_meta.size());
    meta_buf.append(encoded_meta);
  }

  // Footer: pointer to meta_count position
  put_numeric<uint32_t>(&meta_buf, meta_start_pos);

  file_writer_->write(meta_buf);
  file_writer_->flush();
  file_writer_->close_file();

  // Update file size
  file_size_ = filesystem::file_size(file_name);

  return RC::SUCCESS;
}

void ObSSTableBuilder::finish_build_block()
{
  string      last_key       = block_builder_.last_key();
  string_view block_contents = block_builder_.finish();
  file_writer_->write(block_contents);
  block_metas_.push_back(BlockMeta(curr_blk_first_key_, last_key, curr_offset_, block_contents.size()));
  // TODO: block aligned to BLOCK_SIZE
  curr_offset_ += block_contents.size();
  block_builder_.reset();
}

shared_ptr<ObSSTable> ObSSTableBuilder::get_built_table()
{
  // TODO: sstable should have more metadata
  shared_ptr<ObSSTable> sstable = make_shared<ObSSTable>(sst_id_, file_writer_->file_name(), comparator_, block_cache_);
  sstable->init();
  return sstable;
}

void ObSSTableBuilder::reset()
{
  block_builder_.reset();
  curr_blk_first_key_.clear();
  if (file_writer_ != nullptr) {
    file_writer_.reset(nullptr);
  }
  block_metas_.clear();
  curr_offset_ = 0;
  sst_id_      = 0;
  file_size_   = 0;
}
}  // namespace oceanbase
