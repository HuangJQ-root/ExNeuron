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

#ifndef _NEU_JSON__H_
#define _NEU_JSON__H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum neu_json_type_ecp {
    NEU_JSON_ECP_UNDEFINE = 0,
    NEU_JSON_ECP_BOOL     = 1,
    NEU_JSON_ECP_INT      = 2,
    NEU_JSON_ECP_FLOAT    = 3,
    NEU_JSON_ECP_STRING   = 4,
} neu_json_type_ecp_e;

/**
 * @enum neu_json_type_e
 * @brief 用于表示JSON数据类型的枚举。
 *
 * 枚举neu_json_type_e定义了多种JSON数据类型，包括基本类型（如整数、字符串）和复杂类型（如对象、数组）。
 */
typedef enum neu_json_type {
    /**
     * @brief 表示未定义的数据类型。
     *
     * NEU_JSON_UNDEFINE通常用来指示一个尚未初始化或未知的数据类型。
     */
    NEU_JSON_UNDEFINE = 0,

    /**
     * @brief 表示整数类型。
     *
     * NEU_JSON_INT用于表示32位或64位整数值。
     */
    NEU_JSON_INT = 1,

    /**
     * @brief 表示位类型（注意：这可能是一个特定于应用的扩展类型）。
     *
     * NEU_JSON_BIT用于表示二进制位值，具体使用取决于上下文。
     */
    NEU_JSON_BIT,

    /**
     * @brief 表示字符串类型。
     *
     * NEU_JSON_STR用于表示UTF-8编码的文本字符串。
     */
    NEU_JSON_STR,

    /**
     * @brief 表示双精度浮点数类型。
     *
     * NEU_JSON_DOUBLE用于表示双精度(64位)浮点数值。
     */
    NEU_JSON_DOUBLE,

    /**
     * @brief 表示单精度浮点数类型。
     *
     * NEU_JSON_FLOAT用于表示单精度(32位)浮点数值。
     */
    NEU_JSON_FLOAT,

    /**
     * @brief 表示布尔值类型。
     *
     * NEU_JSON_BOOL用于表示true或false值。
     */
    NEU_JSON_BOOL,

    /**
     * @brief 表示对象类型。
     *
     * NEU_JSON_OBJECT用于表示键值对集合，即JSON对象。
     */
    NEU_JSON_OBJECT,

    /**
     * @brief 表示有符号8位整数数组类型。
     *
     * NEU_JSON_ARRAY_INT8用于表示由8位有符号整数组成的数组。
     */
    NEU_JSON_ARRAY_INT8,

    /**
     * @brief 表示无符号8位整数数组类型。
     *
     * NEU_JSON_ARRAY_UINT8用于表示由8位无符号整数组成的数组。
     */
    NEU_JSON_ARRAY_UINT8,

    /**
     * @brief 表示有符号16位整数数组类型。
     *
     * NEU_JSON_ARRAY_INT16用于表示由16位有符号整数组成的数组。
     */
    NEU_JSON_ARRAY_INT16,

    /**
     * @brief 表示无符号16位整数数组类型。
     *
     * NEU_JSON_ARRAY_UINT16用于表示由16位无符号整数组成的数组。
     */
    NEU_JSON_ARRAY_UINT16,

    /**
     * @brief 表示有符号32位整数数组类型。
     *
     * NEU_JSON_ARRAY_INT32用于表示由32位有符号整数组成的数组。
     */
    NEU_JSON_ARRAY_INT32,

    /**
     * @brief 表示无符号32位整数数组类型。
     *
     * NEU_JSON_ARRAY_UINT32用于表示由32位无符号整数组成的数组。
     */
    NEU_JSON_ARRAY_UINT32,

    /**
     * @brief 表示有符号64位整数数组类型。
     *
     * NEU_JSON_ARRAY_INT64用于表示由64位有符号整数组成的数组。
     */
    NEU_JSON_ARRAY_INT64,

    /**
     * @brief 表示无符号64位整数数组类型。
     *
     * NEU_JSON_ARRAY_UINT64用于表示由64位无符号整数组成的数组。
     */
    NEU_JSON_ARRAY_UINT64,

    /**
     * @brief 表示单精度浮点数数组类型。
     *
     * NEU_JSON_ARRAY_FLOAT用于表示由32位浮点数组成的数组。
     */
    NEU_JSON_ARRAY_FLOAT,

    /**
     * @brief 表示双精度浮点数数组类型。
     *
     * NEU_JSON_ARRAY_DOUBLE用于表示由64位浮点数组成的数组。
     */
    NEU_JSON_ARRAY_DOUBLE,

    /**
     * @brief 表示布尔值数组类型。
     *
     * NEU_JSON_ARRAY_BOOL用于表示由布尔值组成的数组。
     */
    NEU_JSON_ARRAY_BOOL,

    /**
     * @brief 表示字符串数组类型。
     *
     * NEU_JSON_ARRAY_STR用于表示由字符串组成的数组。
     */
    NEU_JSON_ARRAY_STR,

    /**
     * @brief 等同于NEU_JSON_UNDEFINE，表示未定义的数据类型。
     */
    NEU_JSON_VALUE = NEU_JSON_UNDEFINE
} neu_json_type_e;

typedef struct {
    int8_t *i8s;
    uint8_t length;
} neu_json_value_array_int8_t;

typedef struct {
    uint8_t *u8s;
    uint8_t  length;
} neu_json_value_array_uint8_t;

typedef struct {
    int16_t *i16s;
    uint8_t  length;
} neu_json_value_array_int16_t;

typedef struct {
    uint16_t *u16s;
    uint8_t   length;
} neu_json_value_array_uint16_t;

typedef struct {
    int32_t *i32s;
    uint8_t  length;
} neu_json_value_array_int32_t;

typedef struct {
    uint32_t *u32s;
    uint8_t   length;
} neu_json_value_array_uint32_t;

typedef struct {
    int64_t *i64s;
    uint8_t  length;
} neu_json_value_array_int64_t;

typedef struct {
    uint64_t *u64s;
    uint8_t   length;
} neu_json_value_array_uint64_t;

typedef struct {
    float  *f32s;
    uint8_t length;
} neu_json_value_array_float_t;

typedef struct {
    double *f64s;
    uint8_t length;
} neu_json_value_array_double_t;

typedef struct {
    bool   *bools;
    uint8_t length;
} neu_json_value_array_bool_t;

typedef struct {
    char **  p_strs;
    uint16_t length;
} neu_json_value_array_str_t;

typedef union neu_json_value {
    int64_t                       val_int;
    uint8_t                       val_bit;
    float                         val_float;
    double                        val_double;
    bool                          val_bool;
    char *                        val_str;
    void *                        val_object;
    neu_json_value_array_int8_t   val_array_int8;
    neu_json_value_array_uint8_t  val_array_uint8;
    neu_json_value_array_int16_t  val_array_int16;
    neu_json_value_array_uint16_t val_array_uint16;
    neu_json_value_array_int32_t  val_array_int32;
    neu_json_value_array_uint32_t val_array_uint32;
    neu_json_value_array_int64_t  val_array_int64;
    neu_json_value_array_uint64_t val_array_uint64;
    neu_json_value_array_float_t  val_array_float;
    neu_json_value_array_double_t val_array_double;
    neu_json_value_array_bool_t   val_array_bool;
    neu_json_value_array_str_t    val_array_str;
} neu_json_value_u;

/**
 * @enum neu_json_attribute_t
 * @brief 用于表示JSON元素属性的枚举。
 *
 * 枚举neu_json_attribute_t定义了JSON元素是否为必需或可选的属性，
 * 这对于验证JSON结构的有效性非常有用。
 */
typedef enum neu_json_attribute {
    /**
     * @brief 表示该JSON元素是必需的。
     *
     * 如果设置了NEU_JSON_ATTRIBUTE_REQUIRED，则在解析JSON时，
     * 必须找到对应的键，否则视为错误。
     */
    NEU_JSON_ATTRIBUTE_REQUIRED,

    /**
     * @brief 表示该JSON元素是可选的。
     *
     * 如果设置了NEU_JSON_ATTRIBUTE_OPTIONAL，则在解析JSON时，
     * 该键可以不存在，不会导致错误。
     */
    NEU_JSON_ATTRIBUTE_OPTIONAL,
} neu_json_attribute_t;

/**
 * @brief 用于表示JSON元素的结构体。
 *
 * 结构体neu_json_elem_t包含了JSON元素的所有相关信息，
 * 如属性、名称、操作名、数据类型、值等，用于解析和处理JSON数据。
 */
typedef struct neu_json_elem {
    /**
     * @brief JSON元素的属性信息。
     *
     * attribute字段存储了关于此JSON元素的一些额外属性信息：JSON元素是否为必须
     */
    neu_json_attribute_t attribute;

    /**
     * @brief JSON元素的名称。
     *
     * name字段指定了JSON对象中该元素的键名。
     */
    char *name;

    /**
     * @brief 操作名（可选）。
     *
     * op_name字段是可选的，通常用于指定与该JSON元素相关的特定操作或标识符。
     */
    char *op_name;

    /**
     * @brief JSON元素的数据类型。
     *
     * t字段指明了该JSON元素的数据类型，例如整数、字符串、布尔值等。
     */
    enum neu_json_type t;

    /**
     * @brief JSON元素的值（联合体）。
     *
     * v字段是一个联合体，可以根据t字段的类型来存储不同的值类型。
     */
    union neu_json_value v;

    /**
     * @brief 精度（仅适用于数值类型）。
     *
     * precision字段用于指定数值类型的精度，比如小数点后的位数。
     */
    uint8_t precision;

    /**
     * @brief 偏差值（仅适用于数值类型）。
     *
     * bias字段用于存储一个偏差值，可以用来调整数值。
     */
    double bias;

    /**
     * @brief 元素解析状态。
     *
     * ok字段指示了在解析过程中是否成功识别并处理了该JSON元素。
     */
    bool ok;
} neu_json_elem_t;

#define NEU_JSON_ELEM_SIZE(elems) sizeof(elems) / sizeof(neu_json_elem_t)

void neu_json_elem_free(neu_json_elem_t *elem);

/* New a empty josn array */
void *neu_json_array();

int neu_json_type_transfer(neu_json_type_e type);

int   neu_json_decode_by_json(void *json, int size, neu_json_elem_t *ele);
int   neu_json_decode(char *buf, int size, neu_json_elem_t *ele);
int   neu_json_decode_array_size_by_json(void *json, char *child);
int   neu_json_decode_array_elem(void *json, int index, int size,
                                 neu_json_elem_t *ele);
int   neu_json_decode_array_by_json(void *json, char *name, int index, int size,
                                    neu_json_elem_t *ele);
int   neu_json_decode_array_size(char *buf, char *child);
int   neu_json_decode_array(char *buf, char *name, int index, int size,
                            neu_json_elem_t *ele);
void *neu_json_decode_new(const char *buf);
void *neu_json_decode_newb(char *buf, size_t len);
void  neu_json_decode_free(void *ob);
int   neu_json_decode_value(void *object, neu_json_elem_t *ele);

void *neu_json_encode_new();
void  neu_json_encode_free(void *json_object);
void *neu_json_encode_array(void *array, neu_json_elem_t *t, int n);
void *neu_json_encode_array_value(void *array, neu_json_elem_t *t, int n);
int   neu_json_encode_field(void *json_object, neu_json_elem_t *elem, int n);
int   neu_json_encode(void *json_object, char **str);

int neu_json_dump_key(void *object, const char *key, char **const result,
                      bool must_exist);
int neu_json_load_key(void *object, const char *key, const char *input,
                      bool must_exist);

#ifdef __cplusplus
}
#endif

#endif
