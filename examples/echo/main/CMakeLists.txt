idf_component_register(SRCS "main.c"
                       REQUIRES esp_wifi nvs_flash
                       PRIV_REQUIRES esp-irc)
