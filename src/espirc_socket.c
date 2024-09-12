// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2024 Danct12
 */

#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

#include "espirc.h"
#include "espirc_socket.h"

#include "esp_err.h"
#include "esp_log.h"

#ifdef CONFIG_ESPIRC_SUPPORT_TLS
#include "esp_tls.h"
#endif

static const char* TAG = "espirc_socket";

esp_err_t espirc_socket_connect(irc_handle_t client)
{
    struct addrinfo hints, *res;

    if (!client->config.host && !client->config.port)
        return ESP_ERR_INVALID_ARG;

    if (client->socket)
        return ESP_ERR_INVALID_STATE;

#ifdef CONFIG_ESPIRC_SUPPORT_TLS
    int port;

    if (client->tls_ptr)
        return ESP_ERR_INVALID_STATE;

    if (client->config.tls) {
        client->tls_ptr = esp_tls_init();
        if (!client->tls_ptr)
            return ESP_ERR_NO_MEM;

        port = atoi(client->config.port);
        if (esp_tls_conn_new_sync(client->config.host, strlen(client->config.host), port,
                &client->config.tls_cfg, client->tls_ptr) < 0)
            goto esp_tls_failure;

        esp_tls_get_conn_sockfd(client->tls_ptr, &client->socket);
    } else
#endif
    {
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(client->config.host, client->config.port, &hints, &res) != 0) {
            ESP_LOGE(TAG, "getaddrinfo failed");
            return ESP_FAIL;
        }

        client->socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (client->socket < 0) {
            ESP_LOGE(TAG, "Socket creation failed (%d)", errno);
            goto plain_failure;
        }

        if (connect(client->socket, res->ai_addr, res->ai_addrlen) < 0) {
            ESP_LOGE(TAG, "Failed to connect (%d)", errno);
            goto plain_failure;
        }

        freeaddrinfo(res);
    }

    return ESP_OK;

#ifdef CONFIG_ESPIRC_SUPPORT_TLS
esp_tls_failure:
    esp_tls_conn_destroy(client->tls_ptr);
    return ESP_FAIL;
#endif

plain_failure:
    freeaddrinfo(res);
    return ESP_FAIL;
}

esp_err_t espirc_socket_close(irc_handle_t client)
{
    int ret;

#ifdef CONFIG_ESPIRC_SUPPORT_TLS
    if (client->tls_ptr)
        ret = esp_tls_conn_destroy(client->tls_ptr);
    else
#endif
        ret = close(client->socket);

    return ret;
}

ssize_t espirc_socket_recv(irc_handle_t client, void *buf, size_t buf_len)
{
    int ret;

#ifdef CONFIG_ESPIRC_SUPPORT_TLS
    if (client->tls_ptr)
        ret = esp_tls_conn_read(client->tls_ptr, buf, buf_len);
    else
#endif
        ret = recv(client->socket, buf, buf_len, 0);

    return ret;
}

ssize_t espirc_socket_write(irc_handle_t client, const void *buf, size_t buf_len)
{
    int ret;

#ifdef CONFIG_ESPIRC_SUPPORT_TLS
    if (client->tls_ptr)
        ret = esp_tls_conn_write(client->tls_ptr, buf, buf_len);
    else
#endif
        ret = write(client->socket, buf, buf_len);

    return ret;
}
