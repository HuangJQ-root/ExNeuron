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

#ifndef _NEU_PLUGIN_REST_HANDLE_H_
#define _NEU_PLUGIN_REST_HANDLE_H_

#include <stdlib.h>

#include "plugin.h"
#include "utils/http_handler.h"

typedef struct neu_rest_handle_ctx neu_rest_handle_ctx_t;

neu_rest_handle_ctx_t *neu_rest_init_ctx(void *plugin);
void                   neu_rest_free_ctx(neu_rest_handle_ctx_t *ctx);
void                  *neu_rest_get_plugin();

void neu_rest_handler(const struct neu_http_handler **handlers, uint32_t *size);
void neu_rest_api_cors_handler(const struct neu_http_handler **handlers,
                               uint32_t *                      size);

#endif
