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
// Created by Wangyunlai.wyl on 2021/5/18.
//

#include "storage/index/index_meta.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "storage/field/field_meta.h"
#include "storage/table/table_meta.h"
#include "json/json.h"

const static Json::StaticString FIELD_NAME("name");
const static Json::StaticString FIELD_FIELD_NAME("field_name");
const static Json::StaticString FIELD_IS_VECTOR_INDEX("is_vector_index");
const static Json::StaticString FIELD_INDEX_TYPE("index_type");
const static Json::StaticString FIELD_DISTANCE_TYPE("distance_type");
const static Json::StaticString FIELD_LISTS("lists");
const static Json::StaticString FIELD_PROBES("probes");

RC IndexMeta::init(const char *name, const FieldMeta &field)
{
  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init index, name is empty.");
    return RC::INVALID_ARGUMENT;
  }

  name_            = name;
  field_           = field.name();
  is_vector_index_ = false;
  index_type_.clear();
  distance_type_.clear();
  lists_  = 0;
  probes_ = 0;
  return RC::SUCCESS;
}

RC IndexMeta::init_vector(const char *name, const FieldMeta &field, const char *index_type,
    const char *distance_type, int lists, int probes)
{
  RC rc = init(name, field);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  is_vector_index_ = true;
  index_type_      = index_type == nullptr ? "IVFFLAT" : index_type;
  distance_type_   = distance_type == nullptr ? "L2_DISTANCE" : distance_type;
  lists_           = lists > 0 ? lists : 1;
  probes_          = probes > 0 ? probes : 1;
  return RC::SUCCESS;
}

void IndexMeta::to_json(Json::Value &json_value) const
{
  json_value[FIELD_NAME]       = name_;
  json_value[FIELD_FIELD_NAME] = field_;
  json_value[FIELD_IS_VECTOR_INDEX] = is_vector_index_;
  if (is_vector_index_) {
    json_value[FIELD_INDEX_TYPE]    = index_type_;
    json_value[FIELD_DISTANCE_TYPE] = distance_type_;
    json_value[FIELD_LISTS]         = lists_;
    json_value[FIELD_PROBES]        = probes_;
  }
}

RC IndexMeta::from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index)
{
  const Json::Value &name_value  = json_value[FIELD_NAME];
  const Json::Value &field_value = json_value[FIELD_FIELD_NAME];
  if (!name_value.isString()) {
    LOG_ERROR("Index name is not a string. json value=%s", name_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  if (!field_value.isString()) {
    LOG_ERROR("Field name of index [%s] is not a string. json value=%s",
        name_value.asCString(), field_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  const FieldMeta *field = table.field(field_value.asCString());
  if (nullptr == field) {
    LOG_ERROR("Deserialize index [%s]: no such field: %s", name_value.asCString(), field_value.asCString());
    return RC::SCHEMA_FIELD_MISSING;
  }

  const Json::Value &is_vector_value = json_value[FIELD_IS_VECTOR_INDEX];
  if (is_vector_value.isBool() && is_vector_value.asBool()) {
    const Json::Value &index_type_value    = json_value[FIELD_INDEX_TYPE];
    const Json::Value &distance_type_value = json_value[FIELD_DISTANCE_TYPE];
    const Json::Value &lists_value         = json_value[FIELD_LISTS];
    const Json::Value &probes_value        = json_value[FIELD_PROBES];
    return index.init_vector(name_value.asCString(), *field,
        index_type_value.isString() ? index_type_value.asCString() : "IVFFLAT",
        distance_type_value.isString() ? distance_type_value.asCString() : "L2_DISTANCE",
        lists_value.isInt() ? lists_value.asInt() : 1,
        probes_value.isInt() ? probes_value.asInt() : 1);
  }

  return index.init(name_value.asCString(), *field);
}

const char *IndexMeta::name() const { return name_.c_str(); }

const char *IndexMeta::field() const { return field_.c_str(); }

void IndexMeta::desc(ostream &os) const
{
  os << "index name=" << name_ << ", field=" << field_;
  if (is_vector_index_) {
    os << ", type=" << index_type_ << ", distance=" << distance_type_
       << ", lists=" << lists_ << ", probes=" << probes_;
  }
}
