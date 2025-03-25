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
config_ **/

#ifndef NEURON_TAG_H
#define NEURON_TAG_H

#include <stdbool.h>
#include <stdint.h>

#include "define.h"
#include "type.h"
#include "utils/utextend.h"
#include "json/json.h"

/**
 * @brief 数据标签属性枚举，用于描述数据标签支持的操作类型。
 *
 * 此枚举定义了数据标签可以具有的不同属性或操作权限，如读取、写入和订阅等。
 */
typedef enum {
    /**
     * @brief 读取属性。
     *
     * 如果一个数据标签具有此属性，则表示可以从该标签中读取数据。
     */
    NEU_ATTRIBUTE_READ      = 1,

    /**
     * @brief 写入属性。
     *
     * 如果一个数据标签具有此属性，则表示可以向该标签写入数据。
     */
    NEU_ATTRIBUTE_WRITE     = 2,

    /**
     * @brief 订阅属性。
     *
     * 如果一个数据标签具有此属性，则表示可以对该标签的数据变化进行订阅，通常用于实时监控。
     */
    NEU_ATTRIBUTE_SUBSCRIBE = 4,
} neu_attribute_e;

typedef enum {
    NEU_DATATAG_ENDIAN_L16  = 0, // #L  2,1
    NEU_DATATAG_ENDIAN_B16  = 1, // #B  1,2
    NEU_DATATAG_ENDIAN_LL32 = 2, // #LL 4,3,2,1
    NEU_DATATAG_ENDIAN_LB32 = 3, // #LB 3,4,1,2
    NEU_DATATAG_ENDIAN_BB32 = 4, // #BB 1,2,3,4
    NEU_DATATAG_ENDIAN_BL32 = 5, // #BL 2,1,4,3
    NEU_DATATAG_ENDIAN_L64  = 6, // #L  8,7,6,5,4,3,2,1
    NEU_DATATAG_ENDIAN_B64  = 7, // #B  1,2,3,4,5,6,7,8
} neu_datatag_endian_e;

typedef enum {
    NEU_DATATAG_STRING_TYPE_H = 0, // high-to-low endian
    NEU_DATATAG_STRING_TYPE_L = 1, // low-to-high endian
    NEU_DATATAG_STRING_TYPE_D = 2, // a high byte is stored in an int16
    NEU_DATATAG_STRING_TYPE_E = 3, // a low byte is stored in an int16
} neu_datatag_string_type_e;

/**
 * @brief 数据标签地址选项联合体，用于描述不同类型数据标签的具体选项。
 *
 * 此联合体根据数据标签的数据类型提供不同的选项配置，包括字节序、默认值标志、长度等。
 */
typedef union {
    /**
     * @brief 16位数值类型的选项。
     *
     * 包含一个字节序属性，适用于16位整数类型（如NEU_TYPE_INT16, NEU_TYPE_UINT16）。
     */
    struct {
        /**
         * @brief 字节序。
         *
         * 指定数据的字节顺序（大端或小端）。
         */
        neu_datatag_endian_e endian;
    } value16;

    /**
     * @brief 32位数值类型的选项。
     *
     * 包含字节序和是否使用默认值标志，适用于32位整数类型
     * （如NEU_TYPE_INT32, NEU_TYPE_UINT32）。
     */
    struct {
        /**
         * @brief 字节序。
         *
         * 指定数据的字节顺序（大端或小端）。
         */
        neu_datatag_endian_e endian;

        /**
         * @brief 是否使用默认值。
         *
         * 如果为true，则表示使用默认值；否则不使用。
         */
        bool                 is_default;
    } value32;

    /**
     * @brief 64位数值类型的选项。
     *
     * 包含一个字节序属性，适用于64位整数类型
     * （如NEU_TYPE_INT64, NEU_TYPE_UINT64）。
     */
    struct {
        /**
         * @brief 字节序。
         *
         * 指定数据的字节顺序（大端或小端）。
         */
        neu_datatag_endian_e endian;
    } value64;

    /**
     * @brief 字符串类型的选项。
     *
     * 包含字符串长度、字符串类型和是否使用默认值标志，
     * 适用于字符串类型（如NEU_TYPE_STRING）。
     */
    struct {
        uint16_t                  length;
        neu_datatag_string_type_e type;
        bool                      is_default;
    } string;

    /**
     * @brief 字节数组类型的选项。
     *
     * 包含数组长度，适用于字节数组类型（如NEU_TYPE_BYTES）。
     */
    struct {
        uint8_t length;
    } bytes;

    /**
     * @brief 单个比特类型的选项。
     *
     * 包含操作标志和比特位置，适用于单个比特类型
     * （如NEU_TYPE_BIT）。
     */
    struct {
        /**
         * @brief 操作标志。
         *
         * 表示是否执行某种特定操作。
         */
        bool    op;

        /**
         * @brief 比特位置。
         *
         * 表示在字节中的比特位置。
         */
        uint8_t bit;
    } bit;
} neu_datatag_addr_option_u;

/**
 * @brief 数据标签结构体，用于描述一个数据标签的各项属性。
 *
 * 此结构体包含了描述一个数据标签所需的各种信息，包括名称、地址、属性、类型、精度、偏置等。
 * 它被用来定义和管理插件中涉及的数据点或信号。
 */
typedef struct {
    /**
     * @brief 数据标签的名称。
     *
     * 该字段存储数据标签的唯一标识符，通常用于引用或识别特定的数据点。
     */
    char *                    name;

    /**
     * @brief 数据标签的地址。
     *
     * 这个字段指定了数据标签在物理设备或模拟环境中的地址或位置。
     */
    char *                    address;

    /**
     * @brief 数据标签的属性。
     *
     * 使用枚举类型`neu_attribute_e`来指定数据标签的属性，例如读写权限等。
     */
    neu_attribute_e           attribute;

    /**
     * @brief 数据标签的数据类型。
     *
     * 使用枚举类型`neu_type_e`来指定数据标签的数据类型，如整型、浮点型等。
     */
    neu_type_e                type;

    /**
     * @brief 数据标签的精度。
     *
     * 指定数据标签值的有效数字位数，主要用于数值类型的格式化。
     */
    uint8_t                   precision;

    /**
     * @brief 小数部分。
     *
     * 用于表示数据标签值的小数部分，特别是在处理浮点数时。
     */
    double                    decimal;

    /**
     * @brief 偏置值。
     *
     * 应用于数据标签值的一个固定偏移量，常用于校正传感器读数等。
     */
    double                    bias;

    /**
     * @brief 数据标签的描述。
     *
     * 提供关于数据标签用途或意义的详细描述，便于理解和维护。
     */
    char *                    description;

    /**
     * @brief 地址选项。
     *
     * 使用联合体`neu_datatag_addr_option_u`来提供针对不同类型地址的具体选项。
     */
    neu_datatag_addr_option_u option;

    /**
     * @brief 元数据。
     *
     * 存储与数据标签相关的额外元数据，长度由`NEU_TAG_META_LENGTH`定义。
     */
    uint8_t                   meta[NEU_TAG_META_LENGTH];

    /**
     * @brief 格式化字符串。
     *
     * 存储用于格式化数据标签值的字符串，长度由`NEU_TAG_FORMAT_LENGTH`定义。
     */
    uint8_t                   format[NEU_TAG_FORMAT_LENGTH];

    /**
     * @brief 格式化字符串的数量。
     *
     * 表示`format`数组中有多少个有效的格式化字符串。
     */
    uint8_t                   n_format;
} neu_datatag_t;

/**
 * @brief 标签元数据结构体，用于描述标签的名称及其关联的数据值。
 *
 * 此结构体包含了标签的基本信息（如名称）以及其对应的数值（包括类型、值和精度），
 * 适用于需要处理带有详细信息的标签数据的情况。
 * 
 * @note
 * precision  0.01
 */
typedef struct neu_tag_meta {
    /**
     * @brief 标签名称。
     *
     * 固定长度的字符数组，存储标签的名称，最大长度由`NEU_TAG_NAME_LEN`定义。
     */
    char         name[NEU_TAG_NAME_LEN];

    /**
     * @brief 标签的数值。
     *
     * 包含标签的具体数值，可能包含不同类型的数据（如整型、浮点型等），并附带精度信息。
     * 使用`neu_dvalue_t`结构体来表示。
     */
    neu_dvalue_t value;
} neu_tag_meta_t;

UT_icd *neu_tag_get_icd();

void neu_tag_format_str(const neu_datatag_t *tag, char *buf, int len);
int  neu_format_from_str(const char *format_str, uint8_t *formats);

neu_datatag_t *neu_tag_dup(const neu_datatag_t *tag);
void           neu_tag_copy(neu_datatag_t *tag, const neu_datatag_t *other);
void           neu_tag_fini(neu_datatag_t *tag);
void           neu_tag_free(neu_datatag_t *tag);

/**
 * @brief 测试标签是否具有指定的属性。
 *
 * 该函数用于检查给定的数据标签是否设置了特定的属性。它通过位运算检查标签的属性字段，
 * 判断是否包含指定的属性类型。
 *
 * @param tag 指向 neu_datatag_t 结构体的指针，表示需要测试的标签。
 * @param attribute 需要测试的属性类型（枚举 neu_attribute_e 的一个值）。
 * @return 如果标签具有指定的属性，则返回 true；否则返回 false。
 */
inline static bool neu_tag_attribute_test(const neu_datatag_t *tag,
                                          neu_attribute_e      attribute)
{
    // 使用位运算检查标签的属性字段是否包含指定的属性类型
    return (tag->attribute & attribute) == attribute;
}

/**
 * @brief Special usage of parsing tag address, e.g. setting length of string
 * type, setting of endian.
 *
 * @param[in] datatag contains all the information about the tag.
 * @param[in] option contain actions that the label can perform.
 * @return  0 on success, -1 on failure.
 */
int neu_datatag_parse_addr_option(const neu_datatag_t *      datatag,
                                  neu_datatag_addr_option_u *option);

bool neu_datatag_string_is_utf8(char *data, int len);

int neu_datatag_string_htol(char *str, int len);
int neu_datatag_string_ltoh(char *str, int len);
int neu_datatag_string_etod(char *str, int len);
int neu_datatag_string_dtoe(char *str, int len);
int neu_datatag_string_etoh(char *str, int len);
int neu_datatag_string_dtoh(char *str, int len);
int neu_datatag_string_tod(char *str, int len, int buf_len);
int neu_datatag_string_toe(char *str, int len, int buf_len);

#endif
