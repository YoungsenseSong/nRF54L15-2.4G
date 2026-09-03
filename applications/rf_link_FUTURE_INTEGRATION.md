# nRF V2 软件预集成：四路同步采集与 ZYNQ 汇聚接口说明

> 当前实现状态（2026-09-03）：本轮仅实现和验证CH0，不展开四路。真实SPIS00、
> 高有效电平DRDY和GPIOTE20-GPPI/DPPI-TIMER20 SYNC capture已在独立CH0配置中实现。
> FPGA控制面实板已到PEEK；header CRC已修正为CRC-16/CCITT-FALSE并重新烧录RX0，
> 但FPGA断电后尚未重新Program复验。最短当前入口是
> [`../docs/PROJECT_CONTEXT.md`](../docs/PROJECT_CONTEXT.md)。

## 1. 本版定位

本版在现有 `YoungsenseSong/nRF54L15-2.4G` 单路 V1 成熟链路上增加接收端 V2
软件预集成结构，目标是让四块 RX nRF 以后能够在 ZYNQ 公共 SYNC 基准下形成统一逻辑
起点，并通过命令式SPIS接口向ZYNQ交付带时间戳、epoch、逻辑样本号和CRC的
记录。

本版没有修改TX双核采集链路，也没有修改204字节空中帧。默认RX、UART二进制
抓帧 RX 和 V2 预集成 RX 是三个独立构建配置。

```text
IIM-42352 -> TX FLPR -> 4096样本共享内存 -> TX CPUAPP
  -> 204字节V1 ESB帧
  -> RX ESB IRQ：只读载荷、取rx_tick、无等待入队
  -> RX线程：校验、16->32位序号扩展、缺失区间、逻辑样本号
  -> sync_manager：epoch、SYNC捕获、BATCH_START逻辑起点
  -> 64条frame_queue
  -> 248字节FPGA记录（头CRC16 + 原始rf_frame + payload CRC32）
  -> 当前调试自动提交 / 未来ZYNQ命令式SPIS
```

## 2. 已实现功能

### 2.1 IRQ退让

ESB事件回调运行在直接IRQ上下文。本版回调只执行：

1. 从ESB RX FIFO读出载荷；
2. 通过统一`timebase`接口取得`rx_tick`；
3. 将固定大小记录以`K_NO_WAIT`投入静态消息队列；
4. 更新原子计数器。

协议校验、CRC、序号扩展、同步算法和 transport 处理均在主线程执行。V2 预集成配置的
IRQ退让队列为64条；默认RX为16条。队列满时丢弃新记录并增加
`radio_queue_overflow`，不会覆盖旧记录。

### 2.2 V1序号扩展和缺失区间

`rx_reorder`保留magic、固定204字节长度和`sample_count=1..96`校验，并增加：

- 16位RF序号回绕到32位`extended_frame_seq`；
- 正常连续、前向跳变、重复帧和过晚帧分类；
- `rf_lost_frames`和估计的`rf_lost_samples`；
- 显式`rx_missing_range`；
- 每个有效帧的`logical_sample_index`；
- V1无法知道每个丢失帧真实`sample_count`时设置`INDEX_ESTIMATED`。

估计缺失样本只用于保持后续逻辑索引单调，并明确带有估计标志；不会复制上一条
样本伪装成有效数据。

### 2.3 静态帧队列

V2 预集成队列默认静态分配64条`rx_frame_record`，每条240字节，保存完整原始
`rf_frame`和接收元数据。提供`push`、`peek`、显式`commit`、`clear`、`level`
和`high_water`。队列使用短临界区自旋锁保护，禁止动态内存和静默覆盖。

当前调试后端在验证FPGA记录CRC后自动commit，因此不会占住队首。未来SPIS后端
必须在ZYNQ发送匹配`transport_seq`的`COMMIT_RECORD`或`DROP_RECORD`后才释放
队首。

### 2.4 同步状态机

状态包括：

```text
SYNC_IDLE -> SYNC_ARMED -> SYNC_WAIT_START -> SYNC_ALIGNING -> SYNC_LOCKED
                                           \-> SYNC_DEGRADED / SYNC_ERROR
```

流程如下：

1. `ARM_SYNC(epoch)`清空旧队列和旧起点；
2. 软件注入或未来硬件捕获记录`sync_tick`；
3. 状态进入`SYNC_WAIT_START`；
4. SYNC后的第一个有效`BATCH_START`帧建立`start_sample_index`；
5. 输出索引变为`local_sample_index - start_sample_index`；
6. 超时、大范围序号跳变或队列溢出进入`SYNC_DEGRADED`，诊断继续运行。

每次重新ARM更新epoch并增加`resync_count`。

### 2.5 FPGA记录和命令协议

固定248字节记录为：

```text
40字节fpga_record_header
204字节原始rf_frame V1
4字节payload_crc32
```

头部包含magic、transport版本、node_id、record_type、32位transport_seq、
sync_epoch、64位rx_tick、64位logical_sample_index、状态标志、payload长度和
header CRC16。

软件协议引擎已定义：

```text
GET_INFO       GET_STATUS       PEEK_RECORD
READ_RECORD    COMMIT_RECORD    DROP_RECORD
CLEAR_STATS    ARM_SYNC         START_STREAM
STOP_STREAM    RESET_LINK
```

`PEEK_RECORD`和`READ_RECORD`不释放队首；错误序号COMMIT返回错误；重复COMMIT、
非法命令和CRC错误分别计数；`transport_seq`按32位自然回绕。

## 3. 调试配置与CH0硬件配置

`future.conf`保留软件预集成/自动提交用途；它可以验证reorder、queue、record builder和
命令所有权，不代表物理SPI。`ch0_spis.conf`配合`ch0_spis.overlay`才启用：

- SPIS00真实Zephyr/nrfx slave backend；
- 8-byte request和260-byte response的两次独立CS事务；
- pending record存在时保持高的DRDY；
- PEEK不释放、匹配transport sequence的COMMIT/DROP才释放；
- P1.09 GPIOTE20 event经GPPI/DPPI直接触发TIMER20 CC1 capture；
- GET_INFO/GET_STATUS和SPI/parser/short-transfer计数器。

物理backend已经不再返回`-ENOTSUP`。但“驱动实现且构建成功”“nRF实板启动成功”和
“ZYNQ完整record闭环成功”仍是三个独立结论。

## 4. Connect Kit Rev.A CH0冻结引脚

依据Rev.A pinout/原理图、Nordic引脚能力、本机NCS devicetree和ATK-DF7020 V3.9
原理图交叉审计，CH0使用：

| 功能 | nRF GPIO | Connect Kit J4 | ZYNQ J4/package pin |
| --- | --- | --- | --- |
| SCK | P2.01 / SPIS00 SCK | J4-9 | J4-12 / T9 |
| MOSI/SDI | P2.04 / SPIS00 SDI | J4-12 | J4-4 / T5 |
| MISO/SDO | P2.02 / SPIS00 SDO | J4-10 | J4-6 / U7 |
| CS_N | P2.05 / SPIS00 CSN | J4-13 | J4-8 / V8 |
| DRDY | P1.10 GPIO output | J4-31 | J4-10 / U8 |
| SYNC_IN | P1.09 GPIOTE20 | J4-30 | J4-14 / V6 |
| RESET_N | dedicated reset，预留 | J4-38 | J4-16 / Y6，首轮不接 |
| GND | GND | J4-39 | J4-37或J4-39 |

RX0 `VDD_GPIO`用户实测约3.3 V；ZYNQ Bank13 VCCO为3.3 V并使用LVCMOS33。两侧不
通过排针互相供电。RESET_N的外部驱动方式和最小脉宽仍UNKNOWN，首轮保持不接。

## 5. 当前仍为V1空中协议

空中帧仍是固定204字节，TX和RX无线参数不变：

- 16位RF序号；
- 96个`int16_t`样本槽；
- MEMS、BATCH_START、BATCH_END标志；
- 毫秒时间戳；
- channel 40、4 Mbps优选、CRC16、no-ACK。

`protocol_version=1`、`node_id`、32位扩展序号、epoch、`rx_tick`和逻辑索引目前是
RX内部/SPIS外层元数据，不占用V1空中帧。

## 6. 逻辑起点不等于物理同时采样

本版可以让四路输出都以SYNC后第一个有效`BATCH_START`为逻辑索引0，并明确标记
缺失区间。这只解决统一逻辑起点和整数样本对齐。

它不能证明四块远端IIM-42352在同一物理时刻采样，因为当前无线链路是单向
no-ACK数据流，TX没有公共采样启动命令，也没有高分辨率采样时间戳。晶振频偏会
使四路长期漂移。

## 7. 工程 V4：第二代空中协议建议字段

保持204字节固定长度的前提下，下一阶段至少应显式加入：

- `protocol_version`和`node_id`；
- 32位`frame_seq`和`batch_seq`；
- `sample_start_index`；
- 高分辨率`sample_time_ticks`；
- `sync_epoch`或START epoch标识；
- 语义头CRC；
- 三轴完整分组约束。

第二代空中协议需要 TX/RX 同时升级并重新评估每帧样本数，不属于本版提交范围。

## 8. 构建与测试

```powershell
west build -p always --sysbuild -d build_rf_link_tx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_tx

west build -p always -d build_rf_link_rx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx

west build -p always -d build_rf_link_rx_future `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=future.conf"

west build -p always -d build_codex_ch0_spis_crcfix `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=ch0_spis.conf" `
     "-DEXTRA_DTC_OVERLAY_FILE=ch0_spis.overlay"

python -m unittest discover -s tests -p test_rf_link_future.py -v
```

需要验证关闭调试transport仍不影响RF接收/重排时，可使用：

```powershell
west build -p always -d build_rf_link_rx_future_no_transport `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=future_no_transport.conf"
```

该配置仍执行 V2 预集成的接收、重排和同步逻辑，但队列记录会由 null 路径立即 commit，
不会等待或输出任何transport数据。

当前离线结果：

| 构建 | FLASH | RAM | 状态 |
| --- | ---: | ---: | --- |
| 旧RX基线（改造前） | 约59,484 B | 19,200 B | 历史基线 |
| 默认 RX 经 IRQ 退让改造后 | 62,396 B | 22,776 B | 通过 |
| V2 预集成 RX | 68,860 B | 48,960 B | 通过 |
| V2 预集成 RX，无 transport | 65,852 B | 48,624 B | 通过 |
| CH0 SPIS CRC修正版 | 79,672 B | 53,400 B | clean build通过；已烧录RX0 |
| CH0 SPIS submit并发修正版 | 79,708 B | 53,416 B | clean build通过；尚未烧录/实板复验 |

V2 预集成配置相对默认 RX 的 RAM 增加26,184 B，主要来自64条 IRQ 退让记录和64条240字节
frame queue；无堆分配。主机契约测试覆盖序号回绕、前向跳变、重复/过晚帧、64帧
满队列、PEEK/COMMIT、同步状态机、CRC、transport序号回绕和关闭transport场景。
当前Python测试为14/14 PASS；生产C ztest已编译/链接，但主机缺QEMU，不能写成C
runtime PASS。

## 9. 状态计数器

V2 预集成 RX 每秒按故障域输出：

- `RADIO`：events、frames、read_errors、queue_overflow；
- `REORDER`：extended_frame_seq、rf_lost_frames、rf_lost_samples、
  rf_duplicates、rf_late_frames、index_estimated；
- `SYNC`：state、sync_capture_count、sync_timeout、sync_epoch、sync_locked、
  resync_count；
- `QUEUE`：push_total、pop_total、overflow、high_water、level；
- `SPI_TRANSPORT`：records、crc_errors、invalid_cmd、duplicate_commit、
  submit_errors、stall_ms、req_xfer、rsp_xfer、spi_errors、parser_errors、short_xfer；
- `CONTROL`：ARM、START、STOP、RESET和非法命令计数。

## 10. 硬件验收顺序

1. 已完成：默认TX/RX无线回归和30分钟q64基线。
2. 已完成：V2调试自动提交后端验证与CH0物理引脚/电压冻结。
3. 已完成到PEEK：真实SPIS、DRDY、GET_INFO/STATUS、ARM_SYNC、START_STREAM。
4. 当前下一步：重新Program FPGA，复验CRC-16/CCITT-FALSE record header PASS。
5. 随后验证重复PEEK、正确COMMIT、错误和重复COMMIT，确认DRDY和队列所有权。
6. 注入SYNC并用逻辑分析仪核对GPIOTE-DPPI-TIMER timestamp及首个BATCH_START index0。
7. 完成至少10,000条连续record和2小时CH0运行，保留nRF、FPGA和波形证据。
8. CH0全部通过后才进入四路规划，不得把本阶段结果扩写成四路物理同步完成。
