# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2024 Danct12

idf_component_register(
    SRCS
        "src/espirc.c"
        "src/espirc_socket.c"
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src
    REQUIRES esp-tls esp_event
)
