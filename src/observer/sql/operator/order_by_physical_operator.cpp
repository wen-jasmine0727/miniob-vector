/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/order_by_physical_operator.h"
#include "common/log/log.h"

using namespace std;
using namespace common;

OrderByPhysicalOperator::OrderByPhysicalOperator(vector<OrderByUnit> &&order_by_units)
    : order_by_units_(std::move(order_by_units))
{}

int OrderByPhysicalOperator::compare_rows(const Tuple &a, const Tuple &b) const
{
  for (auto &unit : order_by_units_) {
    Value val_a, val_b;
    RC rc_a = unit.expression->get_value(a, val_a);
    RC rc_b = unit.expression->get_value(b, val_b);
    if (OB_FAIL(rc_a) || OB_FAIL(rc_b)) {
      continue;
    }
    int cmp = val_a.compare(val_b);
    if (cmp != 0) {
      return unit.is_asc ? cmp : -cmp;
    }
  }
  return 0;
}

RC OrderByPhysicalOperator::open(Trx *trx)
{
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  PhysicalOperator *child = children_[0].get();
  RC rc = child->open(trx);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  while (OB_SUCC(rc = child->next())) {
    Tuple *tuple = child->current_tuple();
    if (nullptr == tuple) {
      LOG_WARN("child current_tuple is null");
      return RC::INTERNAL;
    }
    auto row = make_unique<ValueListTuple>();
    rc = ValueListTuple::make(*tuple, *row);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to materialize tuple: %s", strrc(rc));
      return rc;
    }
    sorted_tuples_.emplace_back(std::move(row));
  }

  if (rc != RC::RECORD_EOF) {
    LOG_WARN("failed to get next from child: %s", strrc(rc));
    return rc;
  }

  child->close();

  sort(sorted_tuples_.begin(), sorted_tuples_.end(),
       [this](const unique_ptr<ValueListTuple> &a, const unique_ptr<ValueListTuple> &b) {
         return compare_rows(*a, *b) < 0;
       });

  if (!sorted_tuples_.empty()) {
    int cell_num = sorted_tuples_[0]->cell_num();
    for (int i = 0; i < cell_num; i++) {
      TupleCellSpec spec;
      sorted_tuples_[0]->spec_at(i, spec);
      tuple_schema_.append_cell(spec);
    }
  }

  current_index_ = -1;
  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::next()
{
  current_index_++;
  if (current_index_ >= static_cast<int>(sorted_tuples_.size())) {
    return RC::RECORD_EOF;
  }
  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::close()
{
  sorted_tuples_.clear();
  tuple_schema_ = TupleSchema();
  current_index_ = -1;
  return RC::SUCCESS;
}

Tuple *OrderByPhysicalOperator::current_tuple()
{
  if (current_index_ < 0 || current_index_ >= static_cast<int>(sorted_tuples_.size())) {
    return nullptr;
  }
  return sorted_tuples_[current_index_].get();
}

RC OrderByPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  schema = tuple_schema_;
  return RC::SUCCESS;
}
