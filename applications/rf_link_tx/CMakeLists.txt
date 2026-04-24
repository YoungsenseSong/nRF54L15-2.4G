#
# Copyright (c) 2018 Nordic Semiconductor
# Copyright (c) 2016-2025 Makerdiary <https://makerdiary.com>
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rf_link_tx)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/fatal_diag.c
  src/main.c
  src/debug_uart.c
  src/ipc_bridge.c
  src/radio_link.c
  src/tx_queue.c
)

target_include_directories(app PRIVATE src)

# NORDIC SDK APP END
