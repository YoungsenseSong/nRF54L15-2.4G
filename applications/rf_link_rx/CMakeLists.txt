#
# Copyright (c) 2018 Nordic Semiconductor
# Copyright (c) 2016-2025 Makerdiary <https://makerdiary.com>
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rf_link_rx)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/debug_uart.c
  src/radio_link.c
  src/rx_reorder.c
  src/rx_sample_stream.c
)

target_include_directories(app PRIVATE src)

# NORDIC SDK APP END
