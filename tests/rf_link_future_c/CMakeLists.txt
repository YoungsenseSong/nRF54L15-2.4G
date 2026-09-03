cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rf_link_future_c)

set(RF_LINK_RX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../applications/rf_link_rx)

target_sources(app PRIVATE
  src/main.c
  ${RF_LINK_RX_DIR}/src/fpga_spi_transport.c
  ${RF_LINK_RX_DIR}/src/fpga_transport.c
  ${RF_LINK_RX_DIR}/src/frame_queue.c
  ${RF_LINK_RX_DIR}/src/link_control.c
  ${RF_LINK_RX_DIR}/src/rx_reorder.c
  ${RF_LINK_RX_DIR}/src/sync_manager.c
)

target_include_directories(app PRIVATE
  ${RF_LINK_RX_DIR}/src
  ${RF_LINK_RX_DIR}/../rf_link_common
)

target_compile_definitions(app PRIVATE
  CONFIG_RF_LINK_DEBUG_UART_TRANSPORT=1
  CONFIG_RF_LINK_FPGA_SPIS=0
  CONFIG_RF_LINK_FPGA_TRANSPORT=1
  CONFIG_RF_LINK_FRAME_QUEUE_DEPTH=64
  CONFIG_RF_LINK_FUTURE_MODE=1
  CONFIG_RF_LINK_NODE_ID=0
  CONFIG_RF_LINK_SYNC_LARGE_GAP_FRAMES=256
  CONFIG_RF_LINK_SYNC_TIMEOUT_MS=1500
)
