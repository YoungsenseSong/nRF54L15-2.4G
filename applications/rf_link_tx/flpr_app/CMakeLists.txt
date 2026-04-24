#
# Copyright (c) 2018 Nordic Semiconductor
# Copyright (c) 2016-2025 Makerdiary <https://makerdiary.com>
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flpr_app)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/adc_sampler.c
  src/fatal_diag.c
  src/ipc_tx.c
  src/main.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# NORDIC SDK APP END
