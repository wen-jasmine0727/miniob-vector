/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2022/6/6.
//

#include "sql/stmt/select_stmt.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "sql/stmt/filter_stmt.h"
#include "storage/db/db.h"
#include "storage/table/table.h"
#include "sql/parser/expression_binder.h"

using namespace std;
using namespace common;

SelectStmt::~SelectStmt()
{
  if (nullptr != filter_stmt_) {
    delete filter_stmt_;
    filter_stmt_ = nullptr;
  }
}

RC SelectStmt::create(Db *db, SelectSqlNode &select_sql, Stmt *&stmt)
{
  if (nullptr == db) {
    LOG_WARN("invalid argument. db is null");
    return RC::INVALID_ARGUMENT;
  }

  BinderContext binder_context;

  // collect tables in `from` statement
  vector<Table *>                tables;
  unordered_map<string, Table *> table_map;
  for (size_t i = 0; i < select_sql.relations.size(); i++) {
    const char *table_name = select_sql.relations[i].c_str();
    if (nullptr == table_name) {
      LOG_WARN("invalid argument. relation name is null. index=%d", i);
      return RC::INVALID_ARGUMENT;
    }

    Table *table = db->find_table(table_name);
    if (nullptr == table) {
      LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }

    binder_context.add_table(table);
    tables.push_back(table);
    table_map.insert({table_name, table});
  }

  // collect query fields in `select` statement
  vector<unique_ptr<Expression>> bound_expressions;
  ExpressionBinder expression_binder(binder_context);
  
  for (unique_ptr<Expression> &expression : select_sql.expressions) {
    RC rc = expression_binder.bind_expression(expression, bound_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  vector<unique_ptr<Expression>> group_by_expressions;
  for (unique_ptr<Expression> &expression : select_sql.group_by) {
    RC rc = expression_binder.bind_expression(expression, group_by_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  // 构建 SELECT 别名映射表（alias -> expression*）
  unordered_map<string, Expression *> alias_map;
  for (unique_ptr<Expression> &expr : bound_expressions) {
    const char *alias = expr->alias();
    if (!common::is_blank(alias) && expr->type() != ExprType::STAR) {
      alias_map[alias] = expr.get();
    }
  }

  // bind order by expressions
  vector<OrderByUnit> order_by_units;
  for (auto &order_by_node : select_sql.order_by) {
    unique_ptr<Expression> bound_expr;

    // 检查 ORDER BY 是否引用了 SELECT 别名
    if (order_by_node.expression->type() == ExprType::UNBOUND_FIELD) {
      auto unbound = static_cast<UnboundFieldExpr *>(order_by_node.expression.get());
      if (common::is_blank(unbound->table_name())) {
        auto it = alias_map.find(unbound->field_name());
        if (it != alias_map.end()) {
          // 使用 SELECT 中对应的表达式作为排序键
          bound_expr = it->second->copy();
        }
      }
    }

    if (!bound_expr) {
      // 正常绑定
      vector<unique_ptr<Expression>> bound_exprs;
      RC rc = expression_binder.bind_expression(order_by_node.expression, bound_exprs);
      if (OB_FAIL(rc)) {
        LOG_INFO("bind order by expression failed. rc=%s", strrc(rc));
        return rc;
      }
      if (bound_exprs.size() != 1) {
        LOG_WARN("order by expression should produce exactly one expression");
        return RC::INVALID_ARGUMENT;
      }
      bound_expr = std::move(bound_exprs[0]);
    }

    OrderByUnit unit;
    unit.expression = std::move(bound_expr);
    unit.is_asc = order_by_node.is_asc;
    order_by_units.emplace_back(std::move(unit));
  }

  Table *default_table = nullptr;
  if (tables.size() == 1) {
    default_table = tables[0];
  }

  // create filter statement in `where` statement
  FilterStmt *filter_stmt = nullptr;
  RC          rc          = FilterStmt::create(db,
      default_table,
      &table_map,
      select_sql.conditions.data(),
      static_cast<int>(select_sql.conditions.size()),
      filter_stmt);
  if (rc != RC::SUCCESS) {
    LOG_WARN("cannot construct filter stmt");
    return rc;
  }

  // everything alright
  SelectStmt *select_stmt = new SelectStmt();

  select_stmt->tables_.swap(tables);
  select_stmt->query_expressions_.swap(bound_expressions);
  select_stmt->filter_stmt_ = filter_stmt;
  select_stmt->group_by_.swap(group_by_expressions);
  select_stmt->order_by_.swap(order_by_units);
  stmt                      = select_stmt;
  return RC::SUCCESS;
}
