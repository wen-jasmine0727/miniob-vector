/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "sql/expr/tuple.h"
#include "sql/operator/physical_operator.h"
#include "storage/index/ivfflat_index.h"
#include "storage/record/record_manager.h"

class Table;

class VectorIndexScanPhysicalOperator : public PhysicalOperator
{
public:
  VectorIndexScanPhysicalOperator(Table *table, IvfflatIndex *index, vector<float> &&query_vector, int limit);
  ~VectorIndexScanPhysicalOperator() override = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::VECTOR_INDEX_SCAN; }
  OpType               get_op_type() const override { return OpType::INDEXSCAN; }

  string param() const override;

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override;

private:
  Table         *table_ = nullptr;
  IvfflatIndex  *index_ = nullptr;
  vector<float>  query_vector_;
  int            limit_ = -1;
  vector<RID>    rids_;
  size_t         current_ = 0;
  Record         current_record_;
  RowTuple       tuple_;
};
