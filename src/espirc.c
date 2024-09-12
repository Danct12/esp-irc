// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2024 Danct12
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <errno.h>

#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "espirc.h"

static const char* TAG = "espirc";

ESP_EVENT_DEFINE_BASE(IRC_EVENTS);

esp_err_t irc_event_handler_register(irc_handle_t client, esp_event_handler_t event_handler,
                                    void *event_handler_arg)
{
    if (!client)
        return ESP_ERR_INVALID_ARG;

    return esp_event_handler_instance_register_with(client->event_handle, IRC_EVENTS,
            IRC_EVENT_ANY, event_handler, event_handler_arg, NULL);
}

esp_err_t irc_event_handler_unregister(irc_handle_t client)
{
    if (!client || !client->event_handle)
        return ESP_ERR_INVALID_ARG;

    return esp_event_handler_instance_unregister_with(client->event_handle, IRC_EVENTS,
            IRC_EVENT_ANY, NULL);
}

static esp_err_t irc_event_post(irc_handle_t client, int32_t event_id, const void *event_data,
                                    size_t event_data_size)
{
    esp_err_t err;

    if (!client || !client->event_handle)
        return ESP_ERR_INVALID_ARG;

    err = esp_event_post_to(client->event_handle, IRC_EVENTS, event_id, event_data,
        event_data_size, portMAX_DELAY);

    /* Notify event loop */
    esp_event_loop_run(client->event_handle, 0);

    return err;
}

static esp_err_t irc_state_set(irc_handle_t client, irc_state_t state)
{
    esp_err_t err;
    if (!client || !client->event_handle)
        return ESP_ERR_INVALID_ARG;

    switch(state) {
        case IRC_STATE_DISCONNECTED:
            client->state = IRC_STATE_DISCONNECTED;
            err = irc_event_post(client, IRC_EVENT_DISCONNECTED, NULL, 0);
            break;
        case IRC_STATE_CONNECTING:
            client->state = IRC_STATE_CONNECTING;
            err = irc_event_post(client, IRC_EVENT_CONNECTING, NULL, 0);
            break;
        case IRC_STATE_CONNECTED:
            client->state = IRC_STATE_CONNECTED;
            err = irc_event_post(client, IRC_EVENT_CONNECTED, NULL, 0);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
            break;
    }

    return err;
}


static irc_message_t* irc_parse_message(char *message) {
    irc_message_t* msgstruct;
    char *token;
    int i;
    i = 0;

    msgstruct = malloc(sizeof(irc_message_t));
    if (!msgstruct) return NULL;

    msgstruct->verb = NULL;
    msgstruct->source = NULL;
    msgstruct->params = NULL;
    msgstruct->colon = 0;

    if (message[0] == ':') {
        token = strtok(message, " :");
        if (token) msgstruct->source = token;
    }

    if (!msgstruct->source) token = strtok(message, " ");
    else token = strtok(NULL, " ");

    msgstruct->verb = token;

    while (token != NULL) {
        token = strtok(NULL, " ");
        if (!token) break;
        msgstruct->params = realloc(msgstruct->params, (i + 1) * sizeof(char*));
        if (token[0] == ':') {
            msgstruct->colon = 1;
            msgstruct->params[i] = malloc((strlen(token)+1)*sizeof(char*));
            strcpy(msgstruct->params[i], token+1);
            token = strtok(NULL, "");
            if (!token) goto trailing_end;
            msgstruct->params[i] = realloc(msgstruct->params[i],
                    sizeof(msgstruct->params[i]) + (strlen(token) + 2) * sizeof(char*));
            strcat(msgstruct->params[i], " ");
            strcat(msgstruct->params[i], token);
        } else {
            msgstruct->params[i] = token;
        }
trailing_end:
        i++;
    }

    msgstruct->params_count = i;

    return msgstruct;
}

static void irc_parse_free(irc_message_t* message)
{
    if (message->colon == 1) free(message->params[message->params_count-1]);
    free(message->params);
    free(message);
}

static void irc_task(void* args) {
    irc_handle_t client = (irc_handle_t) args;
    irc_message_t *msg;
    char *rbufcpy, *split, *splitptr;
    int rbuf_count, offset;
    int sl;

    rbufcpy = NULL;
    offset = 0;
    rbuf_count = 0;

    ESP_LOGD(TAG, "Task started.");
    ESP_LOGD(TAG, "Socket: %d", client->socket);

    client->running = true;

    /*
     * Continously receive data from the server until the client is disconnected
     * from the network.
     */
    while (client->state >= IRC_STATE_CONNECTING &&
            (sl = recv(client->socket, client->rbuf, sizeof(client->rbuf)-1, 0))) {
        if (sl < 0) {
            ESP_LOGE(TAG, "Read socket failed (%d)", errno);
            break;
        }

        if (sl == 0) continue;

        rbuf_count += sl;

        ESP_LOGD(TAG, "Bytes received: %d - Count: %d", sl, rbuf_count);
        rbufcpy = realloc(rbufcpy, 1 + rbuf_count * sizeof(char));
        if (!rbufcpy) {
            ESP_LOGE(TAG, "Failed to realloc mem");
            break;
        }

        ESP_LOGD(TAG, "Offset: %d", offset);

        memcpy(rbufcpy+offset, client->rbuf, sl);
        offset += sl;

        if (client->rbuf[sl-1] != '\n') {
            ESP_LOGD(TAG, "Not enough data");
            continue;
        }

        rbufcpy[rbuf_count] = '\0';

        split = strtok_r(rbufcpy, "\r\n", &splitptr);

        while (split != NULL) {
            if (!strncmp(split, "PING", 4)) {
                split[1] = 'O';
                irc_sendraw(client, split);
            } else if (!strncmp(split, "ERROR", 5)) {
                ESP_LOGE(TAG, "Server error (%s)\n", split);
                irc_disconnect(client);
                break;
            } else {
                msg = irc_parse_message(split);
                if (!msg) continue;

                if (client->state == IRC_STATE_CONNECTING) {
                    /* RPL_WELCOME (001) */
                    if (strncmp(msg->verb, "001", 3) == 0) {
                        irc_state_set(client, IRC_STATE_CONNECTED);
                        if (strlen(client->config.channel) != 0)
                            irc_sendraw(client, "JOIN %s", client->config.channel);
                    }
                    /* ERR_NICKNAMEINUSE (433) */
                    else if (strncmp(msg->verb, "433", 3) == 0) {
                        /* TODO: Add a random number after the nick when 433 is raised */
                        ESP_LOGE(TAG, "Nick is already in use.");
                        irc_disconnect(client);
                    }
                } else {
                    irc_event_post(client, IRC_EVENT_NEW_MESSAGE, msg, sizeof(irc_message_t));
                }

                irc_parse_free(msg);
            }
            split = strtok_r(NULL, "\r\n", &splitptr);
        }
        rbuf_count = 0;
        offset = 0;
    }

    if (rbufcpy)
        free(rbufcpy);

    client->running = false;

    ESP_LOGD(TAG, "Task end.");
    vTaskDelete(NULL);
}

irc_handle_t irc_create(irc_config_t config)
{
    irc_handle_t client;

    if ((!config.host || (config.host && strlen(config.host) == 0)) ||
        (!config.user || (config.user && strlen(config.user) == 0)) ||
        (!config.nick || (config.nick && strlen(config.nick) == 0))) {
        ESP_LOGE(TAG, "IRC host/user/nick is not defined");
        return NULL;
    }

    if (!config.task_priority)
        config.task_priority = tskIDLE_PRIORITY;

    if (!config.task_stack_size)
        config.task_stack_size = 2048;

    if (!config.port || (config.port && strlen(config.port) == 0))
        config.port = "6667";

    if (!config.realname || (config.realname && strlen(config.realname) == 0))
        config.realname = config.nick;

    client = malloc(sizeof(struct irc));
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return NULL;
    }

    ESP_LOGD(TAG, "Allocated %d bytes", sizeof(client));

    client->running = false;

    client->config = config;

    esp_event_loop_args_t loop_args = {
        .queue_size = 1,
        .task_name = NULL
    };

    if (esp_event_loop_create(&loop_args, &client->event_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create event");
        irc_destroy(client);
        return NULL;
    }

    return client;
}


esp_err_t irc_destroy(irc_handle_t client)
{
    if (!client) return ESP_FAIL;

    if(client->event_handle)
        esp_event_loop_delete(client->event_handle);

    free(client);
    return ESP_OK;
}

esp_err_t irc_connect(irc_handle_t client)
{
    struct addrinfo hints, *res;
    int conn;

    if (client->running) {
        ESP_LOGE(TAG, "IRC is running");
        return ESP_FAIL;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (!client->config.host && !client->config.port) {
        ESP_LOGE(TAG, "Host or port not defined.");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Host: %s - Port: %s - User: %s - Nick: %s", client->config.host, client->config.port, client->config.user, client->config.nick);

    if (getaddrinfo(client->config.host, client->config.port, &hints, &res) != 0) {
        ESP_LOGE(TAG, "getaddrinfo failed");
        return ESP_FAIL;
    }

    client->socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (client->socket < 0) {
        ESP_LOGE(TAG, "Socket creation failed (%d)", errno);
        goto res_fail;
    }

    conn = connect(client->socket, res->ai_addr, res->ai_addrlen);
    if (conn < 0) {
        ESP_LOGE(TAG, "Failed to connect (%d)", errno);
        goto res_fail;
    }

    freeaddrinfo(res);

    ESP_LOGD(TAG, "Socket: %d", client->socket);

    client->running = true;

    irc_state_set(client, IRC_STATE_CONNECTING);

    if (xTaskCreate(irc_task, "irc_task", client->config.task_stack_size, client,
            client->config.task_priority, &client->task_handle) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create task");
        client->running = false;
        return ESP_FAIL;
    }

    irc_sendraw(client, "USER %s 0 * :%s", client->config.user, client->config.realname);
    irc_sendraw(client, "NICK %s", client->config.nick);
    return ESP_OK;

res_fail:
    freeaddrinfo(res);
    return ESP_FAIL;
}

esp_err_t irc_disconnect(irc_handle_t client)
{
    if (client->state >= IRC_STATE_CONNECTING) {
        irc_sendraw(client, "QUIT");

        if (close(client->socket) < 0) {
            ESP_LOGE(TAG, "Failed to close socket (%s)", esp_err_to_name(errno));
            return ESP_FAIL;
        }

        irc_state_set(client, IRC_STATE_DISCONNECTED);
    } else {
        ESP_LOGE(TAG, "Not connected to IRC");
        return ESP_FAIL;
    }

    client->running = false;

    return ESP_OK;
}

esp_err_t irc_sendraw(irc_handle_t client, char* fmt, ...)
{
    int endofstring;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(client->sbuf, 512, fmt, ap);
    endofstring = strlen(client->sbuf);
    va_end(ap);

    /*
     * Don't go beyond the buffer.
     * non-IRCv3 buffer size (according to RFC1459) is 512 bytes (incl. CRLF)
     */
    if (endofstring > 510)
        return ESP_ERR_INVALID_ARG;

    ESP_LOGD(TAG, "<< %s", client->sbuf);
    strcpy(client->sbuf+endofstring, "\r\n");
    if (write(client->socket, client->sbuf, strlen(client->sbuf)) < 0) {
        ESP_LOGE(TAG, "Failed to send message (%d)", errno);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}


