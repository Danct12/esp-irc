// SPDX-License-Identifier: CC0-1.0
/*
 * This code is in Public Domain.
 * You can do whatever you want with it.
 */

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "sdkconfig.h"

#include "espirc.h"

#define ADMIN_IDENT CONFIG_EXAMPLE_IRC_ADMIN_HOSTNAME

#if defined(CONFIG_ESPIRC_SUPPORT_TLS) && \
    defined(CONFIG_ESP_TLS_USING_MBEDTLS) && defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
#define CA_BUNDLE_API_SUPPORTED 1
#include "esp_crt_bundle.h"
#endif

irc_handle_t network;

bool connected = false;
static int wifi_retry = 0;

static const char* TAG = "IRC_AdminCheck";

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch(event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                {
                    wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t*) event_data;
                    connected = false;
                    ESP_LOGI(TAG, "Disconnected from %s (BSSID: "MACSTR", Reason: %d)", event->ssid,
                        MAC2STR(event->bssid), event->reason);

                    if (wifi_retry <= CONFIG_EXAMPLE_WIFI_MAXRETRY) {
                        esp_wifi_connect();
                        wifi_retry++;
                    } else {
                        ESP_LOGE(TAG, "Failed to connect after %d retries", wifi_retry);
                        abort();
                    }
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:
                {
                    wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t*) event_data;
                    ESP_LOGI(TAG, "Connected to %s (BSSID: "MACSTR", Channel: %d)", event->ssid,
                        MAC2STR(event->bssid), event->channel);
                    wifi_retry = 0;
                }
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch(event_id) {
            case IP_EVENT_STA_GOT_IP:
                connected = true;
                break;
            default:
                break;
        }
    } else if (event_base == IRC_EVENTS) {
        switch (event_id) {
            case IRC_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnected from IRC");
                break;
            case IRC_EVENT_CONNECTING:
                ESP_LOGI(TAG, "Connecting to IRC");
                break;
            case IRC_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Connected to IRC");
                break;
            case IRC_EVENT_NEW_MESSAGE:
                {
                    irc_message_t* message = (irc_message_t*) event_data;
                    char *nick, *user, *hostname, *source, *target, *pch;
                    int admincheck;

                    if (strncmp(message->verb, "PRIVMSG", 7) == 0 && message->source) {

                        /* Check if the command is "!admin" */
                        if (strncmp(message->params[1], "!admin", 6) != 0) break;

                        pch = (char*) memchr(message->source, '!', strlen(message->source));
                        if (!pch) break;

                        /* Split nick, user and hostname */
                        source = message->source;
                        source[pch-source] = '\0';
                        nick = source;
                        user = source+(pch-source+1);

                        pch = (char*) memchr(user, '@', strlen(user));
                        if (!pch) break;

                        user[pch-user] = '\0';
                        hostname = user+(pch-user+1);

                        target = message->params[0];

                        if (strcmp(nick, target) == 0) break;

                        /*
                         * Check if the target starts with '#' (0x23), '&' (0x26).
                         *
                         * If target doesn't start with these characters, it's
                         * assumed to be a private message and relay them to sender.
                         */
                        switch (target[0]) {
                            case '#':
                            case '&':
                                break;
                            default:
                                target = nick;
                                break;
                        }

                        /* Check user's hostname */
                        admincheck = strcmp(hostname, ADMIN_IDENT);
                        irc_sendraw(network, "PRIVMSG %s :%s: Access %s.",
                                target, nick, (admincheck == 0 ? "Granted" : "Denied"));

                        ESP_LOGI(TAG, "%s - %s (%s)",
                                (admincheck == 0 ? "GRANTED" : "DENIED"),
                                nick, hostname);
                    }
                }
                break;
            default:
                ESP_LOGD(TAG, "Unknown IRC Event");
                break;
        }
    }
}

void app_main(void)
{
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    irc_config_t config = {
        .host = CONFIG_EXAMPLE_IRC_SERVER,
        .port = CONFIG_EXAMPLE_IRC_PORT,
        .user = CONFIG_EXAMPLE_IRC_USER,
        .pass = CONFIG_EXAMPLE_IRC_PASS,
        .nick = CONFIG_EXAMPLE_IRC_NICK,
        .channel = CONFIG_EXAMPLE_IRC_CHANNEL,
        .realname = CONFIG_EXAMPLE_IRC_REALNAME,
#if CONFIG_ESPIRC_SUPPORT_TLS && CONFIG_EXAMPLE_IRC_SSL
        .tls = true,
        .tls_cfg = {
#ifdef CA_BUNDLE_API_SUPPORTED
            .crt_bundle_attach = esp_crt_bundle_attach,
#endif
        },
#endif
    };

    network = irc_create(config);
    if (!network) {
        ESP_LOGE(TAG, "Failed to setup IRC handler");
        return;
    }

    ESP_ERROR_CHECK(irc_event_handler_register(network, &event_handler, NULL));

    while (!connected) {
        ESP_LOGI(TAG, "Waiting for internet connection");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }

    ESP_ERROR_CHECK(irc_connect(network));
}
