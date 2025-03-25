/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2022 EMQ Technologies Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "utils/log.h"
#include "json/json.h"

static double format_tag_value(float ele_value)
{
    double scale    = pow(10, 5);
    double value    = ele_value;
    int    negative = 1;

    if (value < 0) {
        value *= -1;
        negative = -1;
    }

    int64_t integer_part = (int64_t)(value);
    double  decimal_part = value - integer_part;
    decimal_part *= scale;
    decimal_part = round(decimal_part);

    char str[6] = { 0 };
    snprintf(str, sizeof(str), "%05" PRId64 "", (int64_t) decimal_part);
    int i = 0, flag = 0;
    for (; i < 4; i++) {
        if (str[i] == '0' && str[i + 1] == '0') {
            flag = 1;
            break;
        } else if (str[i] == '9' && str[i + 1] == '9') {
            flag = 2;
            break;
        }
    }
    if (flag != 0 && i != 0) {
        decimal_part = round(decimal_part / pow(10, 5 - i));
        value        = (double) integer_part + decimal_part / pow(10, i);
    } else {
        value = (double) integer_part + decimal_part / scale;
    }

    return value * negative;
}

void neu_json_elem_free(neu_json_elem_t *elem)
{
    if (elem == NULL) {
        return;
    }

    switch (elem->t) {
    case NEU_JSON_ARRAY_BOOL:
        free(elem->v.val_array_bool.bools);
        break;
    case NEU_JSON_ARRAY_UINT8:
        free(elem->v.val_array_uint8.u8s);
        break;
    case NEU_JSON_ARRAY_INT8:
        free(elem->v.val_array_int8.i8s);
        break;
    case NEU_JSON_ARRAY_UINT16:
        free(elem->v.val_array_uint16.u16s);
        break;
    case NEU_JSON_ARRAY_INT16:
        free(elem->v.val_array_int16.i16s);
        break;
    case NEU_JSON_ARRAY_UINT32:
        free(elem->v.val_array_uint32.u32s);
        break;
    case NEU_JSON_ARRAY_INT32:
        free(elem->v.val_array_int32.i32s);
        break;
    case NEU_JSON_ARRAY_UINT64:
        free(elem->v.val_array_uint64.u64s);
        break;
    case NEU_JSON_ARRAY_INT64:
        free(elem->v.val_array_int64.i64s);
        break;
    case NEU_JSON_ARRAY_FLOAT:
        free(elem->v.val_array_float.f32s);
        break;
    case NEU_JSON_ARRAY_DOUBLE:
        free(elem->v.val_array_double.f64s);
        break;
    case NEU_JSON_ARRAY_STR:
        for (int i = 0; i < elem->v.val_array_str.length; ++i) {
            free(elem->v.val_array_str.p_strs[i]);
        }
        free(elem->v.val_array_str.p_strs);
        break;
    case NEU_JSON_OBJECT:
        json_decref(elem->v.val_object);
        break;
    case NEU_JSON_STR:
        free(elem->v.val_str);
        break;
    default:
        break;
    }
}
static json_t *encode_object_value(neu_json_elem_t *ele)
{
    json_t *ob = NULL;

    switch (ele->t) {
    case NEU_JSON_BIT:
        ob = json_integer(ele->v.val_bit);
        break;
    case NEU_JSON_INT:
        ob = json_integer(ele->v.val_int);
        break;
    case NEU_JSON_STR:
        ob = json_string(ele->v.val_str);
        break;
    case NEU_JSON_DOUBLE:
        ob = json_realp(ele->v.val_double, ele->precision);
        break;
    case NEU_JSON_FLOAT: {
        double t = ele->v.val_float;
        if (ele->precision == 0 && ele->bias == 0) {
            t = format_tag_value(ele->v.val_float);
        }
        ob = json_realp(t, ele->precision);
        break;
    }
    case NEU_JSON_BOOL:
        ob = json_boolean(ele->v.val_bool);
        break;
    default:
        break;
    }

    return ob;
}

int neu_json_type_transfer(neu_json_type_e type)
{
    switch (type) {
    case NEU_JSON_BOOL:
        return NEU_JSON_ECP_BOOL;
    case NEU_JSON_INT:
    case NEU_JSON_BIT:
        return NEU_JSON_ECP_INT;
    case NEU_JSON_FLOAT:
    case NEU_JSON_DOUBLE:
        return NEU_JSON_ECP_FLOAT;
    case NEU_JSON_STR:
        return NEU_JSON_ECP_STRING;
    default:
        break;
    }

    return NEU_JSON_ECP_STRING;
}

static json_t *encode_object(json_t *object, neu_json_elem_t ele)
{
    json_t *ob = object;

    switch (ele.t) {
    case NEU_JSON_BIT:
        json_object_set_new(ob, ele.name, json_integer(ele.v.val_bit));
        break;
    case NEU_JSON_INT:
        json_object_set_new(ob, ele.name, json_integer(ele.v.val_int));
        break;
    case NEU_JSON_STR:
        json_object_set_new(ob, ele.name, json_string(ele.v.val_str));
        break;
    case NEU_JSON_FLOAT: {
        double t = ele.v.val_float;
        if (ele.precision == 0 && ele.bias == 0) {
            t = format_tag_value(ele.v.val_float);
        }
        json_object_set_new(ob, ele.name, json_realp(t, ele.precision));
        break;
    }
    case NEU_JSON_DOUBLE:
        json_object_set_new(ob, ele.name,
                            json_realp(ele.v.val_double, ele.precision));
        break;
    case NEU_JSON_BOOL:
        json_object_set_new(ob, ele.name, json_boolean(ele.v.val_bool));
        break;
    case NEU_JSON_ARRAY_BOOL: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_bool.length; i++) {
            json_array_append_new(array,
                                  json_boolean(ele.v.val_array_bool.bools[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_INT8: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_int8.length; i++) {
            json_array_append_new(array,
                                  json_integer(ele.v.val_array_int8.i8s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_UINT8: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_uint8.length; i++) {
            json_array_append_new(array,
                                  json_integer(ele.v.val_array_uint8.u8s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_INT16: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_int16.length; i++) {
            json_array_append_new(array,
                                  json_integer(ele.v.val_array_int16.i16s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_UINT16: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_uint16.length; i++) {
            json_array_append_new(array,
                                  json_integer(ele.v.val_array_uint16.u16s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_INT32: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_int32.length; i++) {
            json_array_append_new(array,
                                  json_integer(ele.v.val_array_int32.i32s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_UINT32: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_uint32.length; i++) {
            json_array_append_new(array,
                                  json_integer(ele.v.val_array_uint32.u32s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_INT64: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_int64.length; i++) {
            json_array_append_new(array,
                                  json_integer(ele.v.val_array_int64.i64s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_UINT64: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_uint64.length; i++) {
            json_array_append_new(array,
                                  json_integer(ele.v.val_array_uint64.u64s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_FLOAT: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_float.length; i++) {
            json_array_append_new(array,
                                  json_real(ele.v.val_array_float.f32s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_DOUBLE: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_double.length; i++) {
            json_array_append_new(array,
                                  json_real(ele.v.val_array_double.f64s[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_ARRAY_STR: {
        void *array = json_array();

        for (int i = 0; i < ele.v.val_array_str.length; i++) {
            json_array_append_new(array,
                                  json_string(ele.v.val_array_str.p_strs[i]));
        }

        json_object_set_new(ob, ele.name, array);
        break;
    }
    case NEU_JSON_OBJECT:
        json_object_set_new(ob, ele.name, ele.v.val_object);
        break;
    default:
        break;
    }

    return ob;
}

static json_t *encode_object_ecp(json_t *object, neu_json_elem_t ele)
{
    json_t *ob = object;

    switch (ele.t) {
    case NEU_JSON_BIT:
        json_object_set_new(ob, ele.name, json_integer(ele.v.val_bit));
        break;
    case NEU_JSON_INT:
        json_object_set_new(ob, ele.name, json_integer(ele.v.val_int));
        break;
    case NEU_JSON_STR:
        json_object_set_new(ob, ele.name, json_string(ele.v.val_str));
        break;
    case NEU_JSON_FLOAT: {
        double t = ele.v.val_float;
        if (ele.precision == 0 && ele.bias == 0) {
            t = format_tag_value(ele.v.val_float);
        }
        json_object_set_new(ob, ele.name, json_realp(t, ele.precision));
        break;
    }
    case NEU_JSON_DOUBLE:
        json_object_set_new(ob, ele.name,
                            json_realp(ele.v.val_double, ele.precision));
        break;
    case NEU_JSON_BOOL:
        json_object_set_new(ob, ele.name, json_boolean(ele.v.val_bool));
        break;

#define ENCODE_ARRAY_TO_STRING(TYPE, FIELD, DATA, FORMAT)                      \
    case TYPE: {                                                               \
        size_t buffer_size = 2;                                                \
        for (int i = 0; i < ele.v.FIELD.length; i++) {                         \
            buffer_size += snprintf(NULL, 0, FORMAT, ele.v.FIELD.DATA[i]) + 2; \
        }                                                                      \
        char *array_str = (char *) malloc(buffer_size);                        \
        if (array_str == NULL) {                                               \
            return NULL;                                                       \
        }                                                                      \
        char *ptr = array_str;                                                 \
        *ptr++    = '[';                                                       \
        for (int i = 0; i < ele.v.FIELD.length; i++) {                         \
            int len = sprintf(ptr, FORMAT, ele.v.FIELD.DATA[i]);               \
            ptr += len;                                                        \
            if (i < ele.v.FIELD.length - 1) {                                  \
                *ptr++ = ',';                                                  \
                *ptr++ = ' ';                                                  \
            }                                                                  \
        }                                                                      \
        *ptr++ = ']';                                                          \
        *ptr   = '\0';                                                         \
        json_object_set_new(ob, ele.name, json_string(array_str));             \
        free(array_str);                                                       \
        break;                                                                 \
    }

        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_BOOL, val_array_bool, bools, "%d")
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_INT8, val_array_int8, i8s,
                               "%" PRId8)
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_UINT8, val_array_uint8, u8s,
                               "%" PRIu8)
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_INT16, val_array_int16, i16s,
                               "%" PRId16)
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_UINT16, val_array_uint16, u16s,
                               "%" PRIu16)
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_INT32, val_array_int32, i32s,
                               "%" PRId32)
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_UINT32, val_array_uint32, u32s,
                               "%" PRIu32)
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_INT64, val_array_int64, i64s,
                               "%" PRId64)
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_UINT64, val_array_uint64, u64s,
                               "%" PRIu64)
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_FLOAT, val_array_float, f32s,
                               "%.6f")
        ENCODE_ARRAY_TO_STRING(NEU_JSON_ARRAY_DOUBLE, val_array_double, f64s,
                               "%.6f")

#undef ENCODE_ARRAY_TO_STRING

    case NEU_JSON_ARRAY_STR: {
        size_t buffer_size = 2;
        for (int i = 0; i < ele.v.val_array_str.length; i++) {
            buffer_size +=
                snprintf(NULL, 0, "\"%s\"", ele.v.val_array_str.p_strs[i]) + 2;
        }

        char *array_str = (char *) malloc(buffer_size);
        if (array_str == NULL) {
            return NULL;
        }

        char *ptr = array_str;
        *ptr++    = '[';

        for (int i = 0; i < ele.v.val_array_str.length; i++) {
            int len = sprintf(ptr, "\"%s\"", ele.v.val_array_str.p_strs[i]);
            ptr += len;

            if (i < ele.v.val_array_str.length - 1) {
                *ptr++ = ',';
                *ptr++ = ' ';
            }
        }

        *ptr++ = ']';
        *ptr   = '\0';

        json_object_set_new(ob, ele.name, json_string(array_str));
        free(array_str);
        break;
    }
    case NEU_JSON_OBJECT: {
        char *json_str = json_dumps(ele.v.val_object, JSON_ENCODE_ANY);
        if (json_str == NULL) {
            return NULL;
        }

        json_object_set_new(ob, ele.name, json_string(json_str));
        free(json_str);
        break;
    }
    default:
        break;
    }

    return ob;
}

/**
 * @brief 解析JSON对象中的单个元素值到目标结构体
 * 
 * @param root JSON根对象（必须为JSON Object或Array类型）
 * @param ele  目标元素描述结构体，定义要提取的字段名、类型和存储位置
 * @return int 解析结果：
 *             - 0: 成功（或可选字段不存在）
 *             - -1: 必填字段缺失或类型不匹配
 * 
 * @note
 * - 支持自动类型推断（当ele->t为NEU_JSON_UNDEFINE时）
 * - 处理字符串时会进行深拷贝（需调用方释放内存）
 * - 数组类型会自动分配内存（需调用方释放）
 */
static int decode_object(json_t *root, neu_json_elem_t *ele)
{
    json_t *ob = NULL;

    // 步骤1：定位目标JSON节点 --------------------------------
    // 如果元素名为空，直接使用根对象
    if (ele->name == NULL) {
        ob = root;
    } else { // 否则按名称查找，并支持备用字段名(op_name)
        ob = json_object_get(root, ele->name);
        if (ob == NULL && ele->op_name != NULL && strlen(ele->op_name) > 0 &&
            strlen(ele->op_name) < 32) {
            ob = json_object_get(root, ele->op_name); // 尝试备用名（操作名）
        }
    }

    // 步骤2：检查节点是否存在 --------------------------------
    if (ob == NULL) {
        if (ele->attribute == NEU_JSON_ATTRIBUTE_OPTIONAL) {
            return 0;
        }
        zlog_error(neuron, "json decode: %s failed", ele->name);
        return -1;
    }

    // 步骤3：自动推断类型（如果未指定类型）-------------------
    if (ele->t == NEU_JSON_UNDEFINE) {
        if (json_is_string(ob)) {
            ele->t = NEU_JSON_STR;
        } else if (json_is_real(ob)) {
            ele->t = NEU_JSON_DOUBLE;
        } else if (json_is_boolean(ob)) {
            ele->t = NEU_JSON_BOOL;
        } else if (json_is_integer(ob)) {
            ele->t = NEU_JSON_INT;
        } else if (json_is_array(ob)) {
            json_t *value = NULL;
            int     index = 0;
            ele->t        = NEU_JSON_ARRAY_INT64;
            json_array_foreach(ob, index, value)
            {
                if (json_is_string(value)) {
                    ele->t = NEU_JSON_ARRAY_STR;
                    break;
                }
                if (json_is_real(value)) {
                    ele->t = NEU_JSON_ARRAY_DOUBLE;
                    break;
                }
                if (json_is_boolean(value)) {
                    ele->t = NEU_JSON_ARRAY_BOOL;
                    break;
                }
            }
        } else if (json_is_object(ob)) {
            ele->t = NEU_JSON_OBJECT;
        }
    }

    ele->ok = true;  // 标记开始解析

    // 步骤4：按类型解析值 --------------------------------
    switch (ele->t) {
    case NEU_JSON_BIT:
        ele->v.val_bit = json_integer_value(ob);
        break;
    case NEU_JSON_INT:
        ele->v.val_int = json_integer_value(ob);
        break;
    case NEU_JSON_STR: {
        if (!json_is_string(ob)) {
            ele->ok = false;
            zlog_error(neuron, "json decode: %s failed", ele->name);
            return -1;
        }
        const char *str_val = json_string_value(ob);
        if (str_val == NULL) {
            ele->ok = false;
            zlog_error(neuron, "json decode: %s failed", ele->name);
            return -1;
        }
        ele->v.val_str = strdup(str_val);
        break;
    }
    case NEU_JSON_FLOAT:
        if (json_is_integer(ob)) {
            ele->v.val_float = (float) json_integer_value(ob);
        } else {
            ele->v.val_float = json_real_value(ob);
        }
        break;
    case NEU_JSON_DOUBLE:
        if (json_is_integer(ob)) {
            ele->v.val_double = (double) json_integer_value(ob);
        } else {
            ele->v.val_double = json_real_value(ob);
        }
        break;
    case NEU_JSON_BOOL:
        ele->v.val_bool = json_boolean_value(ob);
        break;
    case NEU_JSON_ARRAY_STR: {
        json_t *value = NULL;

        ele->v.val_array_str.length = json_array_size(ob);
        if (ele->v.val_array_str.length > 0) {
            int index = 0;

            ele->v.val_array_str.p_strs =
                calloc(ele->v.val_array_str.length, sizeof(char *));
            json_array_foreach(ob, index, value)
            {
                const char *str_val = json_string_value(value);
                if (str_val != NULL) {
                    ele->v.val_array_str.p_strs[index] = strdup(str_val);
                }
            }
        }

        break;
    }
    case NEU_JSON_ARRAY_BOOL: {
        json_t *value = NULL;

        ele->v.val_array_bool.length = json_array_size(ob);
        if (ele->v.val_array_bool.length > 0) {
            int index = 0;

            ele->v.val_array_bool.bools =
                calloc(ele->v.val_array_bool.length, sizeof(bool));
            json_array_foreach(ob, index, value)
            {
                ele->v.val_array_bool.bools[index] = json_boolean_value(value);
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_UINT8: {
        json_t *value = NULL;

        ele->v.val_array_uint8.length = json_array_size(ob);
        if (ele->v.val_array_uint8.length > 0) {
            int index = 0;

            ele->v.val_array_uint8.u8s =
                calloc(ele->v.val_array_uint8.length, sizeof(int8_t));
            json_array_foreach(ob, index, value)
            {
                if (json_is_real(value)) {
                    ele->v.val_array_uint8.u8s[index] =
                        (uint8_t) json_real_value(value);
                } else {
                    ele->v.val_array_uint8.u8s[index] =
                        json_integer_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_INT8: {
        json_t *value = NULL;

        ele->v.val_array_int8.length = json_array_size(ob);
        if (ele->v.val_array_int8.length > 0) {
            int index = 0;

            ele->v.val_array_int8.i8s =
                calloc(ele->v.val_array_int8.length, sizeof(int8_t));
            json_array_foreach(ob, index, value)
            {
                if (json_is_real(value)) {
                    ele->v.val_array_int8.i8s[index] =
                        (int8_t) json_real_value(value);
                } else {
                    ele->v.val_array_int8.i8s[index] =
                        json_integer_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_UINT16: {
        json_t *value = NULL;

        ele->v.val_array_uint16.length = json_array_size(ob);
        if (ele->v.val_array_uint16.length > 0) {
            int index = 0;

            ele->v.val_array_uint16.u16s =
                calloc(ele->v.val_array_uint16.length, sizeof(int16_t));
            json_array_foreach(ob, index, value)
            {
                if (json_is_real(value)) {
                    ele->v.val_array_uint16.u16s[index] =
                        (uint16_t) json_real_value(value);
                } else {
                    ele->v.val_array_uint16.u16s[index] =
                        json_integer_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_INT16: {
        json_t *value = NULL;

        ele->v.val_array_int16.length = json_array_size(ob);
        if (ele->v.val_array_int16.length > 0) {
            int index = 0;

            ele->v.val_array_int16.i16s =
                calloc(ele->v.val_array_int16.length, sizeof(int16_t));
            json_array_foreach(ob, index, value)
            {
                if (json_is_real(value)) {
                    ele->v.val_array_int16.i16s[index] =
                        (int16_t) json_real_value(value);
                } else {
                    ele->v.val_array_int16.i16s[index] =
                        json_integer_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_UINT32: {
        json_t *value = NULL;

        ele->v.val_array_uint32.length = json_array_size(ob);
        if (ele->v.val_array_uint32.length > 0) {
            int index = 0;

            ele->v.val_array_uint32.u32s =
                calloc(ele->v.val_array_uint32.length, sizeof(int32_t));
            json_array_foreach(ob, index, value)
            {
                if (json_is_real(value)) {
                    ele->v.val_array_uint32.u32s[index] =
                        (uint32_t) json_real_value(value);
                } else {
                    ele->v.val_array_uint32.u32s[index] =
                        json_integer_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_INT32: {
        json_t *value = NULL;

        ele->v.val_array_int32.length = json_array_size(ob);
        if (ele->v.val_array_int32.length > 0) {
            int index = 0;

            ele->v.val_array_int32.i32s =
                calloc(ele->v.val_array_int32.length, sizeof(int32_t));
            json_array_foreach(ob, index, value)
            {
                if (json_is_real(value)) {
                    ele->v.val_array_int32.i32s[index] =
                        (int32_t) json_real_value(value);
                } else {
                    ele->v.val_array_int32.i32s[index] =
                        json_integer_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_UINT64: {
        json_t *value = NULL;

        ele->v.val_array_uint64.length = json_array_size(ob);
        if (ele->v.val_array_uint64.length > 0) {
            int index = 0;

            ele->v.val_array_uint64.u64s =
                calloc(ele->v.val_array_uint64.length, sizeof(int64_t));
            json_array_foreach(ob, index, value)
            {
                if (json_is_real(value)) {
                    ele->v.val_array_uint64.u64s[index] =
                        (uint64_t) json_real_value(value);
                } else {
                    ele->v.val_array_uint64.u64s[index] =
                        json_integer_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_INT64: {
        json_t *value = NULL;

        ele->v.val_array_int64.length = json_array_size(ob);
        if (ele->v.val_array_int64.length > 0) {
            int index = 0;

            ele->v.val_array_int64.i64s =
                calloc(ele->v.val_array_int64.length, sizeof(int64_t));
            json_array_foreach(ob, index, value)
            {
                if (json_is_real(value)) {
                    ele->v.val_array_int64.i64s[index] =
                        (int64_t) json_real_value(value);
                } else {
                    ele->v.val_array_int64.i64s[index] =
                        json_integer_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_FLOAT: {
        json_t *value = NULL;

        ele->v.val_array_float.length = json_array_size(ob);
        if (ele->v.val_array_float.length > 0) {
            int index = 0;

            ele->v.val_array_float.f32s =
                calloc(ele->v.val_array_float.length, sizeof(float));
            json_array_foreach(ob, index, value)
            {
                if (json_is_integer(value)) {
                    ele->v.val_array_float.f32s[index] =
                        (float) json_integer_value(value);
                } else {
                    ele->v.val_array_float.f32s[index] = json_real_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_ARRAY_DOUBLE: {
        json_t *value = NULL;

        ele->v.val_array_double.length = json_array_size(ob);
        if (ele->v.val_array_double.length > 0) {
            int index = 0;

            ele->v.val_array_double.f64s =
                calloc(ele->v.val_array_double.length, sizeof(double));
            json_array_foreach(ob, index, value)
            {
                if (json_is_integer(value)) {
                    ele->v.val_array_double.f64s[index] =
                        (double) json_integer_value(value);
                } else {
                    ele->v.val_array_double.f64s[index] =
                        json_real_value(value);
                }
            }
        }
        break;
    }
    case NEU_JSON_OBJECT:
        ele->v.val_object = ob;
        if (ele->name == NULL) {
            json_incref(ele->v.val_object);
        }
        break;
    default:
        ele->ok = false;
        zlog_error(neuron, "json decode unknown type: %d", ele->t);
        return -1;
    }

    return 0;
}

void *neu_json_array()
{
    return json_array();
}

int neu_json_decode(char *buf, int size, neu_json_elem_t *ele)
{
    json_error_t error;
    json_t *     root = json_loads(buf, 0, &error);

    if (root == NULL) {
        zlog_error(neuron,
                   "json load error, line: %d, column: %d, position: %d, info: "
                   "%s",
                   error.line, error.column, error.position, error.text);
        return -1;
    }

    for (int i = 0; i < size; i++) {
        if (decode_object(root, &ele[i]) == -1) {
            json_decref(root);
            return -1;
        }
    }

    json_decref(root);
    return 0;
}

/**
 * @brief 解析JSON数据到预定义的结构体元素数组
 * 
 * @param json 输入的JSON数据（必须是已解析的json_t对象）
 * @param size 目标元素数组的长度
 * @param ele  目标元素数组指针
 * @return int 解析结果：
 *             - 0: 所有元素解析成功
 *             - -1: 输入参数无效或解析过程中出错
 * 
 * @note
 * - 要求调用方提前解析好json_t对象（如通过json_loads）
 * - 按顺序解析ele数组中的每个元素，遇到第一个错误立即返回
 * - 需要调用方负责最终释放动态分配的内存（如字符串、数组）
 */
int neu_json_decode_by_json(void *json, int size, neu_json_elem_t *ele)
{
    if (json == NULL) {
        zlog_error(neuron, "The param json is NULL");
        return -1;
    }

    for (int i = 0; i < size; i++) {
        if (decode_object(json, &ele[i]) == -1) {
            return -1;
        }
    }

    return 0;
}

int neu_json_decode_array(char *buf, char *name, int index, int size,
                          neu_json_elem_t *ele)
{
    json_t *     child  = NULL;
    json_t *     object = NULL;
    json_error_t error;
    json_t *     root = json_loads(buf, 0, &error);

    if (root == NULL) {
        zlog_error(
            neuron,
            "json load error, line: %d, column: %d, position: %d, info: %s",
            error.line, error.column, error.position, error.text);
        return -1;
    }

    object = json_object_get(root, name);
    if (object == NULL) {
        json_decref(root);
        return -1;
    }

    child = json_array_get(object, index);
    if (child == NULL) {
        json_decref(root);
        return -1;
    }

    for (int i = 0; i < size; i++) {
        if (decode_object(child, &ele[i]) == -1) {
            json_decref(root);
            return -1;
        }
    }

    json_decref(root);
    return 0;
}

int neu_json_decode_array_elem(void *json, int index, int size,
                               neu_json_elem_t *ele)
{
    json_t *child = NULL;

    child = json_array_get(json, index);
    if (child == NULL) {
        return -1;
    }

    for (int i = 0; i < size; i++) {
        if (decode_object(child, &ele[i]) == -1) {
            return -1;
        }
    }

    return 0;
}

int neu_json_decode_array_by_json(void *json, char *name, int index, int size,
                                  neu_json_elem_t *ele)
{
    json_t *child  = NULL;
    json_t *object = NULL;

    object = json_object_get(json, name);
    if (object == NULL) {
        return -1;
    }

    child = json_array_get(object, index);
    if (child == NULL) {
        return -1;
    }

    for (int i = 0; i < size; i++) {
        if (decode_object(child, &ele[i]) == -1) {
            return -1;
        }
    }

    return 0;
}

int neu_json_decode_array_size_by_json(void *json, char *child)
{
    json_t *ob;
    int     ret = -1;

    ob = json_object_get(json, child);
    if (ob != NULL && json_is_array(ob)) {
        ret = json_array_size(ob);
    } else {
        zlog_error(neuron, "json get array object fail, %s", child);
    }

    return ret;
}

int neu_json_decode_array_size(char *buf, char *child)
{
    json_error_t error;
    json_t *     root = json_loads(buf, 0, &error);
    json_t *     ob;
    int          ret = -1;

    if (root == NULL) {
        zlog_error(
            neuron,
            "json load error, line: %d, column: %d, position: %d, info: %s",
            error.line, error.column, error.position, error.text);
        return -1;
    }

    ob = json_object_get(root, child);
    if (ob != NULL && json_is_array(ob)) {
        ret = json_array_size(ob);
    } else {
        zlog_error(neuron, "json get array object fail, %s", child);
    }

    json_decref(root);
    return ret;
}

void *neu_json_encode_array_value(void *array, neu_json_elem_t *t, int n)
{

    if (array == NULL) {
        array = json_array();
    }

    for (int i = 0; i < n; i++) {
        json_array_append_new(array, encode_object_value(&t[i]));
    }

    return array;
}

void *neu_json_encode_array(void *array, neu_json_elem_t *t, int n)
{
    if (array == NULL) {
        array = json_array();
    }

    json_t *ob = json_object();
    for (int j = 0; j < n; j++) {
        encode_object(ob, t[j]);
    }

    json_array_append_new(array, ob);
    return array;
}

void *neu_json_encode_array_ecp(void *array, neu_json_elem_t *t, int n)
{
    if (array == NULL) {
        array = json_array();
    }

    json_t *ob = json_object();
    for (int j = 0; j < n; j++) {
        encode_object_ecp(ob, t[j]);
    }

    json_array_append_new(array, ob);
    return array;
}

void *neu_json_encode_new()
{
    return json_object();
}

void neu_json_encode_free(void *json_object)
{
    json_decref(json_object);
}

int neu_json_encode_field(void *json_object, neu_json_elem_t *elem, int n)
{
    for (int i = 0; i < n; i++) {
        encode_object(json_object, elem[i]);
    }

    return 0;
}

int neu_json_encode(void *json_object, char **str)
{
    *str = json_dumps(json_object, JSON_REAL_PRECISION(16));

    return 0;
}

void *neu_json_decode_new(const char *buf)
{
    json_error_t error;
    json_t *     root = json_loads(buf, JSON_DECODE_ANY, &error);

    if (root == NULL) {
        zlog_error(
            neuron,
            "json load error, line: %d, column: %d, position: %d, info: %s",
            error.line, error.column, error.position, error.text);
        return NULL;
    }

    return root;
}

/**
 * @brief 从给定的缓冲区中解码 JSON 数据
 *
 * @param buf 指向包含 JSON 数据的字符缓冲区的指针。
 * @param len 缓冲区中 JSON 数据的长度（以字节为单位）。
 * @return void* 如果 JSON 数据解析成功，返回指向解析后的 JSON 对象的指针；
 *               如果解析失败，返回 NULL。在使用返回的指针后
 */
void *neu_json_decode_newb(char *buf, size_t len)
{
    json_error_t error;
    json_t *     root = json_loadb(buf, len, 0, &error);

    if (root == NULL) {
        zlog_error(
            neuron,
            "json load error, line: %d, column: %d, position: %d, info: %s",
            error.line, error.column, error.position, error.text);
        return NULL;
    }

    return root;
}

void neu_json_decode_free(void *ob)
{
    json_decref(ob);
}

int neu_json_decode_value(void *object, neu_json_elem_t *ele)
{
    return decode_object(object, ele);
}

int neu_json_dump_key(void *object, const char *key, char **const result,
                      bool must_exist)
{
    int rv        = 0;
    json_t *new   = NULL;
    json_t *value = NULL;

    value = json_object_get(object, key);
    if (NULL == value) {
        if (must_exist) {
            zlog_error(neuron, "json has no key `%s`", key);
            return -1;
        } else {
            *result = NULL;
            return 0;
        }
    }

    if (NULL == (new = json_object()) || json_object_set(new, key, value) < 0 ||
        NULL == (*result = json_dumps(new, JSON_COMPACT))) {
        rv = -1;
    }

    json_decref(new);
    return rv;
}

int neu_json_load_key(void *object, const char *key, const char *input,
                      bool must_exist)
{
    int          rv           = 0;
    json_t *     input_object = NULL;
    json_t *     value        = NULL;
    json_error_t error        = {};

    input_object = json_loads(input, 0, &error);
    if (NULL == input_object) {
        zlog_error(neuron,
                   "json load error, line: %d, column: %d, position: %d, info: "
                   "%s",
                   error.line, error.column, error.position, error.text);
        return -1;
    }

    value = json_object_get(input_object, key);
    if (NULL == value) {
        if (must_exist) {
            zlog_error(neuron, "json has no key `%s`", key);
            rv = -1;
        }
    } else if (json_object_set(object, key, value) < 0) {
        rv = -1;
    }

    json_decref(input_object);
    return rv;
}
