// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2024 Danct12
 */

#ifndef __ESPIRC_H__
#define __ESPIRC_H__

#include "esp_event.h"

typedef enum {
    IRC_STATE_ERROR = -2,
    IRC_STATE_DISCONNECTED = -1,
    IRC_STATE_UNKNOWN,
    IRC_STATE_INIT,
    IRC_STATE_CONNECTING,
    IRC_STATE_CONNECTED
} irc_state_t;

ESP_EVENT_DECLARE_BASE(IRC_EVENTS);

typedef enum {
    IRC_EVENT_ANY = ESP_EVENT_ANY_ID,
    IRC_EVENT_NONE,
    IRC_EVENT_UNKNOWN,
    IRC_EVENT_DISCONNECTED,
    IRC_EVENT_CONNECTING,
    IRC_EVENT_CONNECTED,
    IRC_EVENT_NEW_MESSAGE,
} irc_event_t;

typedef struct {
    char *source;
    char *verb;
    char **params;
    int params_count;
    int colon;
} irc_message_t;

typedef struct {
    /* IRC Config */
    const char* host;
    const char* user;
    const char* pass;
    const char* nick;
    const char* port;

    /* Additional Config */
    const char* realname;
    const char* channel;

    /* IRC Task */
    int sbuf_size;
    size_t task_stack_size;
    uint8_t task_priority;
} irc_config_t;

struct irc {
    bool running;
    char rbuf[513];
    char sbuf[512];

    irc_config_t config;

    /* IRC Socket */
    int socket;

    /* IRC Task */
    irc_state_t state;
    irc_message_t message;
    TaskHandle_t task_handle;
    esp_event_loop_handle_t event_handle;
};

typedef struct irc* irc_handle_t;

/* IRC Handler */
irc_handle_t irc_create(irc_config_t config);
esp_err_t irc_destroy(irc_handle_t client);
esp_err_t irc_event_handler_register(irc_handle_t client, esp_event_handler_t event_handler,
                                    void *event_handler_arg);
esp_err_t irc_event_handler_unregister(irc_handle_t client);

/* IRC Connection */
esp_err_t irc_connect(irc_handle_t client);
esp_err_t irc_disconnect(irc_handle_t client);

/* IRC Send */
esp_err_t irc_sendraw(irc_handle_t client, char* fmt, ...);

#endif
