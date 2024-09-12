// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2024 Danct12
 */

#ifndef __ESPIRC_SOCKET_H__
#define __ESPIRC_SOCKET_H__

#include "espirc.h"
#include "esp_err.h"

esp_err_t espirc_socket_connect(irc_handle_t client);
esp_err_t espirc_socket_close(irc_handle_t client);
ssize_t espirc_socket_recv(irc_handle_t client, void *buf, size_t buf_len);
ssize_t espirc_socket_write(irc_handle_t client, const void *buf, size_t buf_len);
#endif
