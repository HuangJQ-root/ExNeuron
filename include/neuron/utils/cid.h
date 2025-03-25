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

#ifndef _NEU_UTILS_CID_H_
#define _NEU_UTILS_CID_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "define.h"
#include "msg.h"

typedef enum {
    CO        = 0,
    ST        = 1, // boolean
    MX        = 2, // float
    SP        = 3,
    SG        = 4,
    SE        = 5,
    SV        = 6,
    CF        = 7,
    DC        = 8,
    EX        = 9,
    F_UNKNOWN = 100,
} cid_fc_e;

typedef enum {
    BOOLEAN      = 0,  // BOOLEAN
    INT8         = 1,  // INT8
    INT16        = 2,  // INT16
    INT24        = 3,  // INT24
    INT32        = 4,  // INT32
    INT128       = 5,  // INT128
    INT8U        = 6,  // INT8U
    INT16U       = 7,  // INT16U
    INT24U       = 8,  // INT24U
    INT32U       = 9,  // INT32U
    FLOAT32      = 10, // FLOAT32
    FLOAT64      = 11, // FLOAT64
    Enum         = 12, // Enum
    Dbpos        = 13, // Dbpos
    Tcmd         = 14, // Tcmd
    Quality      = 15, // Quality
    Timestamp    = 16, // Timestamp
    VisString32  = 17, // VisString32
    VisString64  = 18, // VisString64
    VisString255 = 19, // VisString255
    Octet64      = 20, // Octet64
    Struct       = 21, // Struct
    EntryTime    = 22, // EntryTime
    Unicode255   = 23, // Unicode255
    Check        = 24, // Check

    T_UNKNOWN = 255,
} cid_basictype_e;

inline static cid_basictype_e decode_basictype(const char *btype)
{
    cid_basictype_e ret = T_UNKNOWN;
    if (strcmp(btype, "BOOLEAN") == 0) {
        ret = BOOLEAN;
    } else if (strcmp(btype, "INT8") == 0) {
        ret = INT8;
    } else if (strcmp(btype, "INT16") == 0) {
        ret = INT16;
    } else if (strcmp(btype, "INT24") == 0) {
        ret = INT24;
    } else if (strcmp(btype, "INT32") == 0) {
        ret = INT32;
    } else if (strcmp(btype, "INT128") == 0) {
        ret = INT128;
    } else if (strcmp(btype, "INT8U") == 0) {
        ret = INT8U;
    } else if (strcmp(btype, "INT16U") == 0) {
        ret = INT16U;
    } else if (strcmp(btype, "INT24U") == 0) {
        ret = INT24U;
    } else if (strcmp(btype, "INT32U") == 0) {
        ret = INT32U;
    } else if (strcmp(btype, "FLOAT32") == 0) {
        ret = FLOAT32;
    } else if (strcmp(btype, "FLOAT64") == 0) {
        ret = FLOAT64;
    } else if (strcmp(btype, "Enum") == 0) {
        ret = Enum;
    } else if (strcmp(btype, "Dbpos") == 0) {
        ret = Dbpos;
    } else if (strcmp(btype, "Tcmd") == 0) {
        ret = Tcmd;
    } else if (strcmp(btype, "Quality") == 0) {
        ret = Quality;
    } else if (strcmp(btype, "Timestamp") == 0) {
        ret = Timestamp;
    } else if (strcmp(btype, "VisString32") == 0) {
        ret = VisString32;
    } else if (strcmp(btype, "VisString64") == 0) {
        ret = VisString64;
    } else if (strcmp(btype, "VisString255") == 0) {
        ret = VisString255;
    } else if (strcmp(btype, "Octet64") == 0) {
        ret = Octet64;
    } else if (strcmp(btype, "Struct") == 0) {
        ret = Struct;
    } else if (strcmp(btype, "EntryTime") == 0) {
        ret = EntryTime;
    } else if (strcmp(btype, "Unicode255") == 0) {
        ret = Unicode255;
    } else if (strcmp(btype, "Check") == 0) {
        ret = Check;
    } else {
        ret = T_UNKNOWN;
    }
    return ret;
}

inline static const char *fc_to_str(cid_fc_e fc)
{
    switch (fc) {
    case CO:
        return "CO";
    case ST:
        return "ST";
    case MX:
        return "MX";
    case SP:
        return "SP";
    case SG:
        return "SG";
    case SE:
        return "SE";
    case SV:
        return "SV";
    case CF:
        return "CF";
    case DC:
        return "DC";
    case EX:
        return "EX";
    default:
        return "F_UNKNOWN";
    }
}

inline static cid_fc_e decode_fc(const char *fc)
{
    cid_fc_e ret = F_UNKNOWN;
    if (strcmp(fc, "CO") == 0) {
        ret = CO;
    } else if (strcmp(fc, "ST") == 0) {
        ret = ST;
    } else if (strcmp(fc, "MX") == 0) {
        ret = MX;
    } else if (strcmp(fc, "SP") == 0) {
        ret = SP;
    } else if (strcmp(fc, "SG") == 0) {
        ret = SG;
    } else if (strcmp(fc, "SE") == 0) {
        ret = SE;
    } else if (strcmp(fc, "SV") == 0) {
        ret = SV;
    } else if (strcmp(fc, "CF") == 0) {
        ret = CF;
    } else if (strcmp(fc, "DC") == 0) {
        ret = DC;
    } else if (strcmp(fc, "EX") == 0) {
        ret = EX;
    } else {
        ret = F_UNKNOWN;
    }

    return ret;
}

typedef struct {
    char da_name[NEU_CID_LEN32]; // optional
    char do_name[NEU_CID_DO_NAME_LEN];
    char lnclass[NEU_CID_LNCLASS_LEN];
    char prefix[NEU_CID_FCDA_PREFIX_LEN]; // optional
    char lninst[NEU_CID_FCDA_INST_LEN];
    char ldinst[NEU_CID_LEN8];

    cid_fc_e fc;

    cid_basictype_e btypes[NEU_TAG_FORMAT_LENGTH]; // for do-[da]
    int             n_btypes;
} cid_fcda_t;

typedef struct {
    char        name[NEU_CID_DATASET_NAME_LEN];
    cid_fcda_t *fcdas;
    int         n_fcda;
} cid_dataset_t;

typedef struct {
    char name[NEU_CID_REPORT_NAME_LEN];
    char id[NEU_CID_REPORT_ID_LEN];
    char dataset[NEU_CID_DATASET_NAME_LEN];
    bool buffered;
    int  intg_pd;
} cid_report_t;

typedef struct {
    char sdi_name[NEU_CID_LEN16];
    char name[NEU_CID_LEN16];

    cid_basictype_e btype;
} cid_dai_t;

typedef struct {
    char            da_name[NEU_CID_LEN32];
    cid_basictype_e btype;
    cid_fc_e        fc;

    cid_basictype_e co_types[16];
    int             n_co_types;
} cid_doi_ctl_t;

typedef struct {
    char name[NEU_CID_DO_NAME_LEN];

    cid_dai_t *dais;
    int        n_dais;

    cid_doi_ctl_t *ctls;
    int            n_ctls;
} cid_doi_t;

typedef struct {
    char lninst[NEU_CID_LNINST_LEN]; // optional
    char lnclass[NEU_CID_LNCLASS_LEN];
    char lntype[NEU_CID_LNTYPE_LEN];
    char lnprefix[NEU_CID_LNPREFIX_LEN]; // optional

    cid_dataset_t *datasets;
    int            n_datasets;

    cid_report_t *reports;
    int           n_reports;

    cid_doi_t *dois;
    int        n_dois;
} cid_ln_t;

typedef struct {
    char      inst[NEU_CID_INST_LEN];
    cid_ln_t *lns;
    int       n_lns;
} cid_ldevice_t;

typedef struct {
    char name[NEU_CID_ACCESS_POINT_NAME_LEN];

    cid_ldevice_t *ldevices;
    int            n_ldevices;
} cid_access_point_t;

typedef struct {
    char name[NEU_CID_IED_NAME_LEN];

    cid_access_point_t *access_points;
    int                 n_access_points;
} cid_ied_t;

/// **************** Template **************** ///
typedef struct {
    char name[NEU_CID_LNO_NAME_LEN];
    char ref_type[NEU_CID_LNO_TYPE_LEN];
} cid_tm_lno_do_t;

typedef struct {
    char id[NEU_CID_ID_LEN];
    char ln_class[NEU_CID_LNCLASS_LEN];

    cid_tm_lno_do_t *dos;
    int              n_dos;
} cid_tm_lno_type_t;

typedef struct {
    char name[NEU_CID_DA_NAME_LEN];
    char ref_type[NEU_CID_REF_TYPE_LEN];

    cid_basictype_e btype;
    cid_fc_e        fc;
} cid_tm_do_da_t;

typedef struct {
    char name[NEU_CID_LEN16];
    char ref_type[NEU_CID_REF_TYPE_LEN];
} cid_tm_sdo_t;

typedef struct {
    char id[NEU_CID_ID_LEN];

    cid_tm_do_da_t *das;
    int             n_das;

    cid_tm_sdo_t *sdos;
    int           n_sdos;
} cid_tm_do_type_t;

typedef struct {
    char name[NEU_CID_DA_NAME_LEN];
    char ref_type[NEU_CID_REF_TYPE_LEN];

    cid_basictype_e btype;
} cid_tm_bda_type_t;

typedef struct {
    char id[NEU_CID_ID_LEN];

    cid_tm_bda_type_t *bdas;
    int                n_bdas;
} cid_tm_da_type_t;

typedef struct {
    cid_tm_lno_type_t *lnotypes;
    int                n_lnotypes;

    cid_tm_do_type_t *dotypes;
    int               n_dotypes;

    cid_tm_da_type_t *datypes;
    int               n_datypes;
} cid_template_t;

typedef struct {
    cid_ied_t      ied;
    cid_template_t cid_template;
} cid_t;

/**
 * @brief 用于存储 CID（可能是某种特定的标识符）数据集信息的结构体。
 * 
 * 该结构体包含了一系列与 CID 数据集相关的信息，如控制标志、设备名称、
 * 实例信息、类信息、缓冲标志、报告名称、报告 ID 以及数据集名称等。
 * 这些信息用于描述和管理工业物联网（IIoT）中的数据采集、报告和处理相
 * 关的配置或状态信息。
 */
typedef struct {
    /**
     * @brief 控制标志。
     * 
     * 用于表示某种控制状态，可能控制数据采集、报告的启动或停止等操作。
     * 当值为 `true` 时，表示启用相关控制；当值为 `false` 时，表示
     * 禁用相关控制。
     */
    bool control;

    /**
     * @brief IED（智能电子设备）的名称。
     * 
     * 存储智能电子设备的名称，长度由 `NEU_CID_IED_NAME_LEN` 宏定义指定。
     * 该名称通常用于唯一标识一个 IED 设备，以便在系统中进行区分和管理。
     */
    char ied_name[NEU_CID_IED_NAME_LEN];

    /**
     * @brief 逻辑设备实例的名称。
     * 
     * 存储逻辑设备实例的名称 逻辑设备实例是 IED 设备中的一个逻辑部分，
     * 用于区分不同的功能或操作单元。
     */
    char ldevice_inst[NEU_CID_LDEVICE_LEN];

    /**
     * @brief 逻辑节点类的名称。
     * 
     * 存储逻辑节点类的名称，逻辑节点类定义了一组相关的功能和数据对象，
     * 用于描述设备的某种特定功能或行为。
     */
    char ln_class[NEU_CID_LNCLASS_LEN];
    
    /**
     * @brief 缓冲标志。
     * 
     * 用于表示是否启用缓冲功能。当值为 `true` 时，表示启用缓冲；
     * 当值为 `false` 时，表示禁用缓冲。缓冲功能可能用于临时存储
     * 数据，以应对网络延迟或数据处理速度不匹配等情况。
     */
    bool buffered;

    /**
     * @brief 报告的名称。
     * 
     * 存储报告的名称，报告名称用于标识不同的报告，方便用户或系统
     * 对报告进行管理和查询。
     */
    char report_name[NEU_CID_REPORT_NAME_LEN];

    /**
     * @brief 报告的 ID。
     * 
     * 存储报告的唯一标识符，报告 ID 可以用于在系统中唯一地标识
     * 一个报告，便于数据的跟踪和处理。
     */
    char report_id[NEU_CID_REPORT_ID_LEN];

    /**
     * @brief 数据集的名称。
     * 
     * 存储数据集的名称，数据集名称用于标识不同的数据集，方便用户
     * 或系统对数据集进行管理和查询。
     */
    char dataset_name[NEU_CID_DATASET_NAME_LEN];
} cid_dataset_info_t;

int  neu_cid_parse(const char *path, cid_t *cid);
void neu_cid_free(cid_t *cid);

void neu_cid_to_msg(char *driver, cid_t *cid, neu_req_add_gtag_t *cmd);

char *              neu_cid_info_to_string(cid_dataset_info_t *info);
cid_dataset_info_t *neu_cid_info_from_string(const char *str);

#ifdef __cplusplus
}
#endif

#endif