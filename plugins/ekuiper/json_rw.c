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

#include <jansson.h>

#include "neuron.h"
#include "utils/log.h"

#include "json_rw.h"
#include "plugin_ekuiper.h"

int json_encode_read_resp_header(void *json_object, void *param)
{
    int                      ret    = 0;
    json_read_resp_header_t *header = param;

    neu_json_elem_t resp_elems[] = { {
                                         .name      = "node_name",
                                         .t         = NEU_JSON_STR,
                                         .v.val_str = header->node_name,
                                     },
                                     {
                                         .name      = "group_name",
                                         .t         = NEU_JSON_STR,
                                         .v.val_str = header->group_name,
                                     },
                                     {
                                         .name      = "timestamp",
                                         .t         = NEU_JSON_INT,
                                         .v.val_int = header->timestamp,
                                     } };
    ret = neu_json_encode_field(json_object, resp_elems,
                                NEU_JSON_ELEM_SIZE(resp_elems));

    return ret;
}

int json_encode_read_resp_tags(void *json_object, void *param)
{
    int                       ret           = 0;
    json_read_resp_t *        resp          = param;
    neu_plugin_t *            plugin        = resp->plugin;
    neu_reqresp_trans_data_t *trans_data    = resp->trans_data;
    void *                    values_object = NULL;
    void *                    errors_object = NULL;
    void *                    metas_object  = NULL;

    metas_object = neu_json_encode_new();
    if (NULL == metas_object) {
        plog_error(plugin, "ekuiper cannot allocate json object");
        return -1;
    }

    values_object = neu_json_encode_new();
    if (NULL == values_object) {
        plog_error(plugin, "ekuiper cannot allocate json object");
        json_decref(metas_object);
        return -1;
    }
    errors_object = neu_json_encode_new();
    if (NULL == errors_object) {
        plog_error(plugin, "ekuiper cannot allocate json object");
        json_decref(values_object);
        json_decref(metas_object);
        return -1;
    }

    utarray_foreach(trans_data->tags, neu_resp_tag_value_meta_t *, tag_value)
    {
        neu_json_read_resp_tag_t json_tag = { 0 };

        neu_tag_value_to_json(tag_value, &json_tag);

        neu_json_elem_t tag_elem = {
            .name      = json_tag.name,
            .t         = json_tag.t,
            .v         = json_tag.value,
            .precision = tag_value->value.precision,
        };

        if (json_tag.n_meta > 0) {
            void *meta = neu_json_encode_new();
            for (int k = 0; k < json_tag.n_meta; k++) {
                neu_json_elem_t meta_elem = {
                    .name = json_tag.metas[k].name,
                    .t    = json_tag.metas[k].t,
                    .v    = json_tag.metas[k].value,
                };
                neu_json_encode_field(meta, &meta_elem, 1);
            }

            neu_json_elem_t meta_elem = {
                .name         = json_tag.name,
                .t            = NEU_JSON_OBJECT,
                .v.val_object = meta,
            };
            neu_json_encode_field(metas_object, &meta_elem, 1);
            free(json_tag.metas);
        }

        ret = neu_json_encode_field((0 != json_tag.error) ? errors_object
                                                          : values_object,
                                    &tag_elem, 1);
        if (0 != ret) {
            json_decref(errors_object);
            json_decref(values_object);
            json_decref(metas_object);
            return ret;
        }
    }

    neu_json_elem_t resp_elems[] = {
        {
            .name         = "values",
            .t            = NEU_JSON_OBJECT,
            .v.val_object = values_object,
        },
        {
            .name         = "errors",
            .t            = NEU_JSON_OBJECT,
            .v.val_object = errors_object,

        },
        {
            .name         = "metas",
            .t            = NEU_JSON_OBJECT,
            .v.val_object = metas_object,
        },
    };
    // steals `values_object` and `errors_object`
    ret = neu_json_encode_field(json_object, resp_elems,
                                NEU_JSON_ELEM_SIZE(resp_elems));

    return ret;
}

int json_encode_read_resp(void *json_object, void *param)
{
    int                       ret        = 0;
    json_read_resp_t *        resp       = param;
    neu_plugin_t *            plugin     = resp->plugin;
    neu_reqresp_trans_data_t *trans_data = resp->trans_data;

    json_read_resp_header_t header = { .group_name = trans_data->group,
                                       .node_name  = trans_data->driver,
                                       .timestamp  = global_timestamp };

    ret = json_encode_read_resp_header(json_object, &header);
    if (0 != ret) {
        plog_error(plugin,
                   "ekuiper fail encode data header node:%s group:%s, %" PRIu64,
                   header.node_name, header.group_name, header.timestamp);
        return ret;
    }

    ret = json_encode_read_resp_tags(json_object, param);

    return ret;
}

/**
 * @brief 解码单条 JSON 写入请求
 *
 * 该函数用于从给定的 JSON 对象中解码单条写入请求的数据，并将其填充到 `neu_json_write_req_t` 结构体中。
 * 它首先定义了需要从 JSON 对象中解析的元素数组，然后调用 `neu_json_decode_by_json` 函数进行解析。
 * 解析完成后，将解析得到的值赋值给 `neu_json_write_req_t` 结构体的相应成员。
 * 如果解析过程中出现错误，会释放已分配的字符串内存。
 *
 * @param json_obj 指向要解析的 JSON 对象的指针，通常是由 `neu_json_decode_newb` 等函数解析得到的 JSON 对象。
 * @param req 指向 `neu_json_write_req_t` 结构体的指针，用于存储解码后的写入请求数据。
 * @return int 解码操作的结果，0 表示成功，非 0 表示失败。失败的原因可能是 JSON 数据格式不符合预期或解析过程中出现其他错误。
 */
static int decode_write_req_json(void *json_obj, neu_json_write_req_t *req)
{
    int ret = 0;
    
    neu_json_elem_t req_elems[] = {
        {
            .name = "node_name",
            .t    = NEU_JSON_STR,
        },
        {
            .name = "group_name",
            .t    = NEU_JSON_STR,
        },
        {
            .name = "tag_name",
            .t    = NEU_JSON_STR,
        },
        {
            .name = "value",
            .t    = NEU_JSON_VALUE,
        },
        {
            .name      = "precision",
            .t         = NEU_JSON_INT,
            .attribute = NEU_JSON_ATTRIBUTE_OPTIONAL,
        },
    };
    ret = neu_json_decode_by_json(json_obj, NEU_JSON_ELEM_SIZE(req_elems),
                                  req_elems);

    if (req_elems[4].v.val_int > 0) {
        req_elems[3].t            = NEU_JSON_DOUBLE;
        req_elems[3].v.val_double = (double_t) req_elems[3].v.val_int;
    }

    req->node  = req_elems[0].v.val_str;
    req->group = req_elems[1].v.val_str;
    req->tag   = req_elems[2].v.val_str;
    req->t     = req_elems[3].t;
    req->value = req_elems[3].v;

    if (ret != 0) {
        free(req_elems[0].v.val_str);
        free(req_elems[1].v.val_str);
        free(req_elems[2].v.val_str);
    }

    return ret;
}

static int decode_write_tags_req_json(void *                     json_obj,
                                      neu_json_write_tags_req_t *req)
{
    int ret = 0;

    neu_json_elem_t req_elems[] = {
        {
            .name = "node_name",
            .t    = NEU_JSON_STR,
        },
        {
            .name = "group_name",
            .t    = NEU_JSON_STR,
        },
        {
            .name = "tags",
            .t    = NEU_JSON_OBJECT,
        },
    };
    ret = neu_json_decode_by_json(json_obj, NEU_JSON_ELEM_SIZE(req_elems),
                                  req_elems);
    if (ret != 0) {
        free(req_elems[0].v.val_str);
        free(req_elems[1].v.val_str);
        return -1;
    }

    req->node  = req_elems[0].v.val_str;
    req->group = req_elems[1].v.val_str;

    req->n_tag = neu_json_decode_array_size_by_json(json_obj, "tags");
    if (req->n_tag <= 0) {
        return 0; // ignore empty array
    }

    req->tags = calloc(req->n_tag, sizeof(req->tags[0]));
    for (int i = 0; i < req->n_tag; i++) {
        neu_json_elem_t v_elems[] = {
            {
                .name = "tag_name",
                .t    = NEU_JSON_STR,
            },
            {
                .name = "value",
                .t    = NEU_JSON_VALUE,
            },
            {
                .name      = "precision",
                .t         = NEU_JSON_INT,
                .attribute = NEU_JSON_ATTRIBUTE_OPTIONAL,
            },
        };

        ret = neu_json_decode_array_by_json(
            json_obj, "tags", i, NEU_JSON_ELEM_SIZE(v_elems), v_elems);

        if (v_elems[2].v.val_int > 0) {
            v_elems[1].t            = NEU_JSON_DOUBLE;
            v_elems[1].v.val_double = (double_t) v_elems[1].v.val_int;
        }

        req->tags[i].tag   = v_elems[0].v.val_str;
        req->tags[i].t     = v_elems[1].t;
        req->tags[i].value = v_elems[1].v;

        if (ret != 0) {
            for (; i >= 0; --i) {
                free(req->tags[i].tag);
                if (NEU_JSON_STR == req->tags[i].t) {
                    free(req->tags[i].value.val_str);
                }
            }
            free(req->node);
            free(req->group);
            free(req->tags);
            req->tags = NULL;
            return ret;
        }
    }

    return 0;
}

/**
 * @brief 解码 JSON 写入请求
 *
 * 该函数用于将传入的 JSON 字符串解析为 `neu_json_write_t` 结构体。它会根据 JSON 数据的结构
 * 判断是单条写入请求还是多条写入请求，并调用相应的解码函数进行处理。
 *
 * @param buf 指向包含 JSON 数据的缓冲区的指针。
 * @param len 缓冲区中 JSON 数据的长度（以字节为单位）。
 * @param result 指向 `neu_json_write_t` 结构体指针的指针，用于存储解码后的结果。
 *               如果解码成功，该指针将指向一个分配好内存的 `neu_json_write_t` 结构体；
 *               如果解码失败，该指针将保持不变。
 * @return int 解码操作的结果，0 表示成功，-1 表示失败。失败的原因可能包括内存分配失败、
 *             JSON 解析失败或具体解码函数执行失败。
 */
int json_decode_write_req(char *buf, size_t len, neu_json_write_t **result)
{
    int               ret      = 0;
    void *            json_obj = NULL;
    neu_json_write_t *req      = calloc(1, sizeof(*req));
    if (req == NULL) {
        return -1;
    }

    // 解析 JSON 数据
    json_obj = neu_json_decode_newb(buf, len);
    if (NULL == json_obj) {
        free(req);
        return -1;
    }

    // 检查 JSON 数据中是否包含 "tags" 字段
    if (NULL == json_object_get(json_obj, "tags")) {
        // 如果不包含 "tags" 字段，认为是单条写入请求
        req->singular = true;

        // 调用解码单条写入请求的函数
        ret           = decode_write_req_json(json_obj, &req->single);
    } else {
        // 如果包含 "tags" 字段，认为是多条写入请求
        req->singular = false;

        // 调用解码多条写入请求的函数
        ret           = decode_write_tags_req_json(json_obj, &req->plural);
    }

    // 根据解码结果处理
    if (0 == ret) {
        *result = req;
    } else {
        free(req);
    }

    neu_json_decode_free(json_obj);
    return ret;
}
