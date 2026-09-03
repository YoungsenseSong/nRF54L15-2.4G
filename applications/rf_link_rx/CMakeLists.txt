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
  src/timebase.c
)

target_sources_ifdef(CONFIG_RF_LINK_FUTURE_MODE app PRIVATE
  src/frame_queue.c
  src/sync_manager.c
  src/fpga_transport.c
  src/fpga_spi_transport.c
  src/link_control.c
)

target_sources_ifdef(CONFIG_RF_LINK_FPGA_SPIS app PRIVATE
  src/fpga_spis_backend.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../rf_link_common
)

# NORDIC SDK APP END
