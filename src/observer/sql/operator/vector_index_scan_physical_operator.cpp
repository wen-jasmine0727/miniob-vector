/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "sql/operator/vector_index_scan_physical_operator.h"

#include "storage/table/table.h"

VectorIndexScanPhysicalOperator::VectorIndexScanPhysicalOperator(
    Table *table, IvfflatIndex *index, vector<float> &&query_vector, int limit)
    : table_(table), index_(index), query_vector_(std::move(query_vector)), limit_(limit)
{}

RC VectorIndexScanPhysicalOperator::open(Trx *)
{
  if (table_ == nullptr || index_ == nullptr || limit_ <= 0) {
    return RC::INVALID_ARGUMENT;
  }
  rids_ = index_->ann_search(query_vector_, static_cast<size_t>(limit_));
  current_ = 0;
  tuple_.set_schema(table_, table_->table_meta().field_metas());
  return RC::SUCCESS;
}

RC VectorIndexScanPhysicalOperator::next()
{
  while (current_ < rids_.size()) {
    RID rid = rids_[current_++];
    RC rc = table_->get_record(rid, current_record_);
    if (rc == RC::SUCCESS) {
      tuple_.set_record(&current_record_);
      return RC::SUCCESS;
    }
  }
  return RC::RECORD_EOF;
}

RC VectorIndexScanPhysicalOperator::close()
{
  rids_.clear();
  current_ = 0;
  return RC::SUCCESS;
}

Tuple *VectorIndexScanPhysicalOperator::current_tuple()
{
  tuple_.set_record(&current_record_);
  return &tuple_;
}

string VectorIndexScanPhysicalOperator::param() const
{
  return string(index_->index_meta().name()) + " ON " + table_->name();
}
