# CH0 RF / RX-to-ZYNQ handoff

更新时间：2026-08-18（Asia/Shanghai）

## 1. 仓库与环境

- 分支：`codex/rf-link-2p4g-devlog`
- 审计起点/当前 HEAD：`4e360863a3569c9494197ec701604404519d811d`
- 上游参考：`youngsense=https://github.com/YoungsenseSong/nRF54L15-2.4G.git`
- 工作树包含本次未提交修改；未 reset、未切分支、未覆盖用户修改。
- NCS `v3.1.0`，Zephyr `4.1.99 (ncs-v3.1.0)`，Zephyr SDK `0.17.1`，west `1.5.0`。
- 目标板：`nrf54l15_connectkit/nrf54l15/cpuapp`。
- RX0 probe：`820D9A5F0F3BA22B8E4ES`，主 UART `COM11`，IF shell `COM12`。
- TX0 probe：`820D9A5F0F3CEA5784A8F`，主 UART `COM10`，IF shell `COM9`。
- TX 是 sysbuild 双镜像：CPUAPP `rf_link_tx` + FLPR `flpr_app`；RX 是 CPUAPP 单镜像。

已读取根 README、`applications/rf_link_README.md`、DESIGN、ROADMAP、FUTURE_INTEGRATION、INTEGRATION_REPORT、2.4G 指南和 Connect Kit 构建/烧录说明。

## 2. 当前无线协议（未改变 RF 参数）

- ESB DPL，pipe 0，base address 0 `{0x52,0x46,0x4c,0x31}`，pipe-0 prefix `0x54`，地址长 5。
- channel 40；nRF54L15 实测/启动日志为 4 Mbit/s，代码在无 4 Mbit/s 支持时编译期回退 2 Mbit/s。
- ESB CRC16；no-ACK；TX `retransmit_count=0`。应用 frame 内没有额外 CRC 字段。
- `rf_frame` 固定 204 bytes（packed、当前 nRF wire image 为 little-endian）：
  - offset 0: magic `0xA55A`, u16
  - offset 2: sequence, u16
  - offset 4: sample_count, u16
  - offset 6: flags, u16
  - offset 8: timestamp_ms, u32
  - offset 12: 96 x int16 samples，共 192 bytes
- flags：BIT1 MEMS，BIT2 BATCH_START，BIT3 BATCH_END（BIT0 为 test）。
- 每批 4096 samples：42 帧 x 96 + 末帧 64；采样流 12,000 samples/s（3 axes x 4 kHz）。

最终启动标识已加入但不改变地址值：

```text
mode=ESB_PRX,phy=4M,ack=noack,channel=40,pipe=0,prefix=0x54,fw=v0.5-rf-throughput-optimization-8-g4e360863a356,samples_per_frame=96
mode=MEMS_BATCH_ESB_PTX,phy=4M,ack=noack,channel=40,pipe=0,prefix=0x54,fw=v0.5-rf-throughput-optimization-8-g4e360863a356,batch_samples=4096,samples_per_frame=96,sample_hz=12000
```

## 3. 构建与软件测试

以下命令均为 0 compile/link error：

```powershell
west build -p always --sysbuild -d build_codex_ch0_tx_final `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_tx

west build -p always -d build_codex_ch0_rx_final `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx

west build -p always -d build_codex_ch0_rx_stream_q64_final `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=stream.conf" "-DEXTRA_DTC_OVERLAY_FILE=stream.overlay"

west build -p always -d build_codex_ch0_rx_future_final `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=future.conf"

west build -p always -d build_codex_ch0_rx_future_no_transport_final `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=future_no_transport.conf"

west build -p always -d build_codex_rf_link_future_c_final `
  -b qemu_cortex_m3 tests\rf_link_future_c
```

最终尺寸：RX default FLASH 62,596 B / RAM 33,144 B；RX stream FLASH 67,132 B / RAM 42,792 B；RX future debug FLASH 69,048 B / RAM 48,968 B；future no-transport FLASH 66,040 B / RAM 48,624 B；TX CPUAPP FLASH 71,460 B / RAM 25,080 B。

软件测试：

- `python -m unittest discover -s tests -p test_rf_link_future.py -v`：13/13 PASS。注意这些是 Python contract/reference-model tests，不是生产 C 的执行结果。
- 新增 `tests/rf_link_future_c`，直接编译生产 C 源，覆盖 layout/CRC、sequence wrap/gap/duplicate/late、64-record queue、PEEK/COMMIT、sync index 0、auto-commit 和错误计数；QEMU test firmware 编译/链接成功。
- C ztest 未执行：`cmake --build build_codex_rf_link_future_c_final\rf_link_future_c --target run` 明确失败于 `QEMU-NOTFOUND`。不得写成“C 单测通过”。

## 4. Gate A：CH0 无线板上证据

最初 q16 短测约 20 s：2,358 valid frames、224,608 samples，`lost=7` 且 `rf_q_ovf=7`，表明 16-depth deferred ISR queue 会造成本地丢包。只改变一个主要变量，将默认 depth 改为 64；RF 参数未变。

q64 30 分钟测试：

- 时间：2026-08-17 23:49:43.466 至 2026-08-18 00:19:45.308，精确跨度 00:30:01.842。
- 原始文件：
  - `D:\nRF54L15\tmp\ch0_rx_q64_30min_20260817_234942.csv`
  - `D:\nRF54L15\tmp\ch0_tx_q64_30min_20260817_234942.csv`
  - 同名 `.stdout.log` / `.stderr.log`
- TX 增量：5,214 batches，224,202 frames，21,356,544 samples；batch header/CRC、IPC、RF、FIFO、slot、fatal 计数均为 0。
- RX 增量：224,158 valid frames，21,352,320 valid samples。
- 无线丢失：44 frames / 4,224 samples；`44 / (224158 + 44) = 0.019625%`。未伪造零丢帧。
- 丢失发生区间：23:57:38–23:58:49（累计 6）；00:01:35–00:03:56（累计 10）；00:05:34–00:12:04（累计 40）；00:14:29–00:15:02（累计 42）；00:19:16–00:19:37（累计 44）。
- duplicate 0；application bad frame 0；ESB payload read error 0；radio queue overflow 0。ESB 驱动没有暴露“被 PHY CRC 丢弃”的独立计数，因此只能报告上述可观测项。
- 30 分钟固件当时尚未输出 queue high-water。新增观测后默认固件短测 20 s：最终 5,972 frames / 568,896 samples，lost/dup/late/bad/read_err/q_ovf 均 0，deferred queue high-water 23/64。
- 实际 sequence 跨过 u16 wrap，连续处理正常。

真实批次结构使用 stream debug build 验证。2 Mbaud 运行时配置返回 `-ENOTSUP`，静态 2 Mbaud 也被驱动以 `Unsupported baudrate` 拒绝；因此基于驱动证据改为 1 Mbaud，并使用异步整块 UARTE 发送。stream overlay 只覆盖现有 `uart30.current-speed`，通过 `EXTRA_DTC_OVERLAY_FILE` 追加，构建日志确认默认应用 board overlay 仍同时加载；没有更改/猜测 UART pin routing。

- `D:\nRF54L15\tmp\ch0_stream_q64_async_1m_500.csv`：500 frames，11 个完整 batch 全部通过，1 个捕获窗口首尾不完整 batch，sequence gap 0，stream drop 0，bad/discarded record 0。
- 最终 overlay 组合复测 `D:\nRF54L15\tmp\ch0_stream_q64_final_300.csv`：300 frames，6 个完整 batch 全部通过，sequence gap 0，stream drop 0。
- 完整 batch 均验证为 42 x 96 + 1 x 64 = 4096 samples，BATCH_START/BATCH_END 合理。

## 5. Phase C：V2 debug/auto-commit 数据路径

仓库现有并已审计：u16→u32 reorder、64-record `frame_queue`、sync manager、248-byte FPGA record builder、PEEK/COMMIT command engine、统计计数器。

真实 RX0 上短测 debug/auto-commit firmware（不是物理 SPI）：

```text
RADIO events=7998 frames=7998 read_errors=0 queue_overflow=0 queue_high_water=43
REORDER extended_frame_seq=42435 rf_lost_frames=0 rf_lost_samples=0 rf_duplicates=0 rf_late_frames=0 index_estimated=0 bad_magic=0 bad_size=0 bad_sample_count=0
SYNC state=0 sync_capture_count=0 sync_timeout=0 sync_epoch=0 sync_locked=0 resync_count=0
QUEUE push_total=7998 pop_total=7998 overflow=0 high_water=1 level=0
SPI_TRANSPORT records=7998 crc_errors=0 invalid_cmd=0 duplicate_commit=0 submit_errors=0 stall_ms=0
```

这证明板上 RF→reorder→queue→record builder→debug auto-commit 可运行；它不证明 PEEK/COMMIT 物理时序，也未 ARM_SYNC。PEEK/COMMIT、fault injection、ARM_SYNC 后首个有效 BATCH_START→logical index 0 目前由 Python tests 和“已编译但未运行”的 C ztest 覆盖。

## 6. 当前 CH0 SPI 代码契约（物理层仍为 provisional）

所有多字节字段目前为 packed nRF native little-endian。

### 8-byte request

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 1 | command code |
| 1 | 3 | reserved = 0 |
| 4 | 4 | argument u32 |

Commands：1 GET_INFO，2 GET_STATUS，3 PEEK_RECORD，4 READ_RECORD，5 COMMIT_RECORD，6 DROP_RECORD，7 CLEAR_STATS，8 ARM_SYNC，9 START_STREAM，10 STOP_STREAM，11 RESET_LINK。

当前 request 没有 CRC。COMMIT/DROP 的 argument 是当前 pending record 的 `transport_seq`；PEEK/READ 不释放队首，匹配的 COMMIT 才释放，错误 sequence 返回 `-ESTALE`，重复 COMMIT 进入 `duplicate_commit`。

### 260-byte response

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | status, int32 |
| 4 | 4 | transport_seq, u32 |
| 8 | 1 | record_valid |
| 9 | 3 | reserved = 0 |
| 12 | 248 | FPGA record |

GET_INFO/GET_STATUS 当前只由 command engine 返回 status/零初始化 response，尚没有真实 SPIS backend 填充专用 info/status payload。

### 248-byte FPGA record

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | magic `0x3146524E` |
| 4 | 1 | version = 1 |
| 5 | 1 | node_id（CH0 当前 future config = 0） |
| 6 | 2 | record_type = 1 |
| 8 | 4 | transport_seq |
| 12 | 4 | sync_epoch |
| 16 | 8 | rx_tick |
| 24 | 8 | logical_sample_index |
| 32 | 4 | status_flags |
| 36 | 2 | payload_len = 204 |
| 38 | 2 | header_crc16 |
| 40 | 204 | 原始 `rf_frame` payload |
| 244 | 4 | payload_crc32 |

- header CRC：CRC-16/CCITT-FALSE，poly `0x1021`、init `0xFFFF`、非反射、
  xorout `0x0000`，覆盖 record bytes 0..37；实现使用 Zephyr
  `crc16(0x1021u, 0xffffu, ...)`。2026-09-03 前曾错误使用 reflected
  `crc16_ccitt()`，已在第 13 节修正。
- payload CRC：Zephyr `crc32_ieee`，仅覆盖 204-byte `rf_frame`（record bytes 40..243）。
- status flags：VALID BIT0，GAP_BEFORE BIT1，INDEX_ESTIMATED BIT2，BATCH_START BIT3，BATCH_END BIT4，SYNCED BIT5，SYNC_DEGRADED BIT6。

## 7. 未实现/未验证，严禁误报

- `spis_backend_init()` 仍返回 `-ENOTSUP`，`ready()` 仍为 false；future/V2 能编译和 debug backend 能运行不等于 SPIS 可用。
- hardware SYNC capture 配置仍命中 compile-time `#error`；软件注入/逻辑测试不等于 GPIOTE–DPPI–TIMER 硬件捕获。
- DRDY GPIO 尚未实现；CPOL/CPHA、最大 SCLK、CS_N 全事务范围、turnaround/dummy bytes、DRDY 极性、SYNC_IN/RESET_N 电平/时序均没有证据，未猜测。
- GET_INFO/GET_STATUS 物理事务、10,000 records、逻辑分析仪五线观测和 2 小时 SPI 长测均未做。
- QEMU 未安装，生产 C ztest 尚未真正执行。
- RX warm-reset 后若 TX sequence 从 0 重启，RX 会把新帧判为 late 直到再次 reset RX；这是已观察到的恢复边界，尚未在本轮改变 reorder 策略。

## 8. 当前板卡状态与准确下一步

- RX0 已烧入 `build_codex_ch0_rx_final` 默认 q64 固件。
- TX0 已烧入 `build_codex_ch0_tx_final` CPUAPP 和 FLPR。此前多次热烧录/reset 后的 IPC 停顿已通过断电冷启动恢复；恢复不是软件修改结果。
- 用户回传的 TX COM10 日志从 `batches_ok=9` 连续增长到至少 `48`，最后一条完整统计为 48 batches / 2,064 frames / 196,608 samples，`batch_bad_hdr/batches_fail/batch_bad_crc/frames_fail/ipc_bad/ipc_drop/release_err/rf_fail/rf_timeout/lp_malformed/lp_io_err/lp_fifo_ovf/lp_slot_wait/lp_fatal` 均为 0，`mac_avg_us=627`。
- 用户回传的 RX COM11 启动标识确认 `ESB_PRX / 4M / noack / channel 40 / pipe 0 / prefix 0x54`。日志连续到至少 4,649 frames / 442,848 samples；最后一条被粘贴截断，但此前完整统计到 4,553 frames / 433,728 samples，`lost/dup/late/bad/rf_read_err/rf_q_ovf` 均为 0，`rf_q_hwm=22/64`。
- 上述冷启动回归解决了“最终 logging build 尚未恢复”的临时阻塞，但只是一次约 36 秒的短测，不能替代已经记录的 q64 30 分钟基线，也不能证明物理 SPI。
- 原始回传日志位于 Codex 附件：
  - TX：`C:\Users\22209\.codex\attachments\46e07fae-37d2-4a80-9cd6-1de30db4dafb\pasted-text.txt`
  - RX：`C:\Users\22209\.codex\attachments\5b40314f-73ca-4947-96a6-ad57da04a0b5\pasted-text.txt`

已确认的硬件身份/参考资料：

- RX0/TX0：nRF54L15 Connect Kit，板级引脚资料采用 `nrf54l15-connect-kit-pinout-diagram_reva.pdf`（Rev.A）。
- FPGA：正点原子领航者 `ATK-DF7020`（ZYNQ-7020）。用户给出的平台简介只能确认板卡系列/资源概览，不能替代该块实物对应版本的底板原理图、核心板原理图、IO 分配表或 Vivado XDC。

下一步准确停在“用户提供接线资料”而不是“修改 overlay”：

1. 提供这块 ATK-DF7020 实物的底板版本和核心板版本照片/丝印，并提供对应的《领航者ZYNQ底板原理图》《ZYNQ_7020核心板原理图》《领航者ZYNQ开发板IO引脚分配表.xlsx》或当前 Vivado 工程 XDC。
2. 指定计划使用的 FPGA 扩展排针编号及物理针号；据原理图确认其 ZYNQ package pin、PL bank、电压轨、板载复用、上下拉和串联器件。
3. 确认/实测 Connect Kit `VDD_GPIO` 当前为 3.3 V 还是 1.8 V，并确认 ATK 对应 PL bank 的 VCCO。两边共地后才能连信号；不得从 nRF 的 VBUS/VSYS 或 FPGA 5 V 排针互相供电。
4. 由 ZYNQ 侧明确 CPOL/CPHA、初始 SCLK、CS_N 建立/保持/间隔、CS 是否覆盖完整 request+response、是否允许一次 CS 下连续传输 268 bytes（2,144 个 SCLK 周期）、DRDY 极性、SYNC_IN 脉宽/边沿和 RESET_N 驱动方式。
5. 资料齐全后再把 SCK/MOSI/MISO/CS_N/DRDY/SYNC_IN/RESET_N 映射到双方物理针脚，并创建独立 CH0 SPIS overlay。

资料确认前不要创建 SPIS board overlay 或实现真实 SPIS/DRDY。资料确认后才进入独立 CH0 overlay、真实 SPIS、level DRDY、逻辑分析仪和 10,000/2h 验收。

## 9. 本次修改摘要

- 默认 deferred radio queue 16→64；新增 `late` 和 radio queue high-water 统计。
- stream 改为有证据支持的静态 1 Mbaud、追加式 stream overlay、异步整块 UARTE TX；更新抓流默认 baud 和文档命令。
- 固化 request=8 / response=260 packed layout 和编译期 size assert。
- 新增直接编译生产 C 模块的最小 ztest suite。
- TX/RX 启动日志增加 pipe/prefix 和 git-derived firmware build id；RF 地址与日志共用常量，数值不变。

## 10. Connect Kit Rev.A / Nordic MCP / ATK-DF7020 只读硬件审计

### Connect Kit Rev.A 引脚图

- 40-pin header 可访问 P2.0..P2.10（物理 8..18）、P1.2..P1.14 的一部分（物理 19..31）、P0.0/P0.1（物理 34/35）以及 P0.2/P0.3/P0.4（物理 5/6/7）；物理 39 为 GND，物理 40 为 VDD_GPIO。
- `VDD_GPIO` 由板上 TPS63901/VSEL 选择：VSEL=0 为 3.3 V，VSEL=1 为 1.8 V；必须以实物状态/测量为准。
- P0.0/P0.1 已被默认 `uart30` console 占用；SWDIO/SWCLK、RESET、IFMCU、NFC/analog 复用脚不能仅凭“已引出”就当作空闲 GPIO。
- 引脚图只证明“引出关系”，不构成最终接线决定。

### Nordic 官方资料与本机 NCS v3.1.0 交叉核对

- nRF54L15 有 SPIS00/20/21/22/30；SPIS00 只能用 P2 dedicated pins，SPIS20/21 可用 P1 或对应 P2 dedicated pins，SPIS22 用 P1，SPIS30 用 P0。所有 SPIS 支持 mode 0..3。
- 产品规格给出的 SPIS 上限为 8 MHz；SCK period 最小 125 ns，CSN→SCK setup 最小 1,000 ns，SCK→CSN hold 最小 1,000 ns，CS inactive 最小 300 ns。实际初始值仍须与 ZYNQ 侧协商，建议首轮低速开始而不是直接按上限。
- 本机 NCS v3.1.0 的 `nrf54l_05_10_15.dtsi` 对 SPI20/21/22/30 标出 `max-frequency=8 MHz`、`easydma-maxcnt-bits=16`；SPIS binding 要求 `def-char`、`pinctrl-0` 和 `pinctrl-names`。
- 官方 QFN pin assignment 显示两组 P2 dedicated SPIS 信号能力：P2.1/P2.4/P2.2/P2.5 分别可作 SCK/SDI/SDO/CSN，P2.6/P2.9/P2.8/P2.10 分别可作另一组 SCK/SDI/SDO/CSN。这只是 SoC 合法候选集，尚未选组、未映射 ATK 排针、未写 overlay。
- P0/P1 最高标称 8 MHz并支持 GPIOTE；P2 可达更高速但没有 GPIO SENSE/DETECT/GPIOTE。最终 SYNC_IN 的 GPIOTE→DPPI→TIMER capture 必须放在 P0 或 P1；普通 GPIO ISR 时间戳不能冒充最终实现。
- GPIO 输入高门限为 0.7×VDD、低门限为 0.3×VDD；当 nRF VDD=1.8 V 时，外部 3.3 V 直接输入超过 `VDD+0.3 V` 绝对最大范围，因此必须先确认双方 IO 电压。若两边均为 3.3 V，也仍需确认具体 FPGA bank VCCO 和 IOSTANDARD。

### ATK-DF7020 当前证据边界

- 用户参考页说明领航者 ZYNQ 平台有 XC7Z020 版本；教程示例还证明某些 PL BANK35 信号使用 3.3 V/LVCMOS33。
- 这不能推出任意扩展排针都是 3.3 V，也不能推出可自由占用。搜索到的资料名称包括底板原理图 V2.6、核心板原理图 V1.5 和 IO 分配表，但当前未拿到用户这块板对应的原文件。
- 因此目前仍没有证据冻结 ATK 侧 SCK/MOSI/MISO/CS_N/DRDY/SYNC_IN/RESET_N package pin，也没有证据冻结 CPOL/CPHA 或 CS 时序。

参考链接：

- Nordic nRF54L15 SPIS：<https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/spis.html>
- Nordic nRF54L15 SPIS timing：<https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/_tmp/nrf54l15/autodita/spis/parameters.elec_spec.html>
- Nordic nRF54L15 GPIO port capabilities：<https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/gpio.html-concept_port_capabilities>
- Nordic nRF54L15 QFN48 pin assignments：<https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/chapters/pin.html-qfn48>
- Zephyr `nordic,nrf-spis` binding：<https://nrfconnectdocs.nordicsemi.com/ncs/latest/zephyr/build/dts/api/bindings/spi/nordic%2Cnrf-spis.html>
- 用户给出的领航者 ZYNQ 平台简介：<https://bbs.elecfans.com/jishu_1991379_1_1.html>

## 11. 2026-08-18 CH0 电气冻结前审计（结合 FPGA handoff 9.9）

本节更新并替代第 8、10 节中“ATK 侧资料尚未确认”的旧结论。已读取
`F:\OliverS\AI_Embedded\F730+FPGA\handoff.md` 第 9.9 节；FPGA 侧已经把
ATK-DF7010/7020P V3.9 底板、CF7010B/7020B 核心板和用户照片交叉核对完成。
CH0 的 FPGA 侧物理分配已确认是 J4-4/6/8/10/12/14/16（T5/U7/V8/U8/T9/V6/Y6），
均位于 Bank13，VCCO=3.3 V，拟用 LVCMOS33；RESET_N 首轮不接。

### 11.1 本次实际读取的 nRF 资料

- `docs/assets/attachments/nrf54l15-connect-kit-schematic_reva.pdf`：图纸号
  `NCK54L15SCH`，Rev.A，2025-05-29，共 6 页。第 2 页为 TPS63901/VSEL 供电，
  第 4 页为 nRF54L15-QFAA 和 package pin，第 5 页为 J4，第 6 页为 LED/按键。
- `docs/assets/attachments/nrf54l15-connect-kit-pinout-diagram_reva.pdf`：Rev.A，1 页，
  用于交叉核对 J4 物理针号。
- Nordic MCP 中的 nRF54L15 产品规格：QFN48 pin assignments、SPIS instances/
  operation/electrical timing、GPIO port capabilities、GPIOTE 和 pin reset。
- 本机 NCS v3.1.0：`zephyr/dts/vendor/nordic/nrf54l_05_10_15.dtsi`、
  `zephyr/drivers/spi/spi_nrfx_spis.c`、Connect Kit board DTS/pinctrl 和 RX overlay。

资料集明确针对 Connect Kit Rev.A，但仅凭仓库文件和 probe ID 不能目视确认 RX0
实物 PCB 的 revision 丝印。因此当前状态是“设计基线=Rev.A，RX0 实物 revision=待用户
照片/丝印确认”，不得把两者混写为已经完成的实物确认。

### 11.2 CH0 nRF 侧候选映射（资料已闭合，仍受 VDD_GPIO 实测 gate）

| 信号 | 方向 | Connect Kit | nRF GPIO | QFAA pin | 端口域 | 功能/占用 | 板载网络 |
| --- | --- | ---: | --- | ---: | --- | --- | --- |
| SCK | ZYNQ→nRF | J4-9 | P2.01 | 12 | MCU/P2 | SPIS00/20 dedicated SCK；RX 当前未占用 | J4 到 U3 直连，未见串阻、上下拉或电平转换 |
| MOSI/SDI | ZYNQ→nRF | J4-12 | P2.04 | 15 | MCU/P2 | SPIS00/20 dedicated SDI；RX 当前未占用 | 同上 |
| MISO/SDO | nRF→ZYNQ | J4-10 | P2.02 | 13 | MCU/P2 | SPIS00/20 dedicated SDO；RX 当前未占用 | 同上 |
| CS_N | ZYNQ→nRF | J4-13 | P2.05 | 16 | MCU/P2 | SPIS00/20 dedicated CSN；RX 当前未占用 | 同上 |
| DRDY | nRF→ZYNQ | J4-31 | P1.10 | 38 | PERI/P1 | 普通 GPIO 输出；RX 当前未占用 | J4 到 U3 直连，未见串阻、上下拉或电平转换 |
| SYNC_IN | ZYNQ→nRF | J4-30 | P1.09 | 37 | PERI/P1 | GPIOTE20 可用（P1 共 8 个 IN channels）；RX 当前未占用 | 同上 |
| RESET_N（预留） | ZYNQ→nRF | J4-38 | nRESET | 30 | RESET/VDD_NRF | 专用低有效 reset；首轮禁止连接 | J4 NRESET 共享 IFMCU reset 控制，并经 R17=1 kΩ 到 SoC；SoC 有内部上拉 |

证据页码：连接器针号来自 Rev.A 原理图 PDF 第 5 页和 pinout PDF 第 1 页；
QFAA package pin 与 dedicated SPIS 功能来自原理图第 4 页和 Nordic QFN48 pin
assignment；VDD/VSEL 来自原理图第 2 页；P0.2 LED、P0.3 按键来自第 6 页。

选择 SPIS00 的第一组合法 dedicated P2 pins，避免当前 `uart30` 使用的 P0.0/P0.1，
也不使用 P2.0 充当 CSN（P2.0 没有 SPIS CSN dedicated function）。P2 本身没有
GPIOTE，因此 SYNC_IN 单独放在 P1.09。P1.08 具有 GRTC 专用候选用途，未选用。
上述信号在当前 RX DTS/overlay 中没有软件占用；这只是映射冻结候选，尚未写入 overlay。

Nordic Revision 1 errata 100 还指出 P1.09/P1.10 在 power-on reset 后约 1 us 可能短接到地；
应用必须等启动完成后才驱动/解释 DRDY 与 SYNC，RESET_N 也不得在 POR 期间由 FPGA 拉低。

### 11.3 VDD_GPIO 硬门槛与测量步骤

Rev.A 原理图第 2 页表明 TPS63901 输出 `VDD_NRF`：VSEL low=3.3 V，VSEL high=1.8 V；
VSEL 还可由 interface MCU 控制。软件配置、历史经验和原理图默认电阻都不能代替实测。
准确测点是 J4-40（pinout 标 `VDD_GPIO`，原理图网络名 `VDD_NRF`），参考地用相邻
J4-39（GND）。

1. FPGA 完全断电，并从 Connect Kit 拔掉所有到 FPGA 的信号线和电源线。
2. 仅用 USB 给 RX0 Connect Kit 上电；不要把 J4-40 当作外部供电脚使用。
3. 万用表置直流电压档（自动量程或至少 5 V 档）。
4. 黑表笔接 J4-39 GND，红表笔接 J4-40 VDD_GPIO，避免短接相邻针。
5. 记录稳定读数（例如 `3.28 V` 或 `1.80 V`），同时拍 RX0 正反面/版本丝印照片回传。
6. 测量完成后断电；在结果审阅前不要接任何 ZYNQ 信号。

- 若约 3.3 V：可以继续评审 3.3 V 直连，但仍需先生成/检查 FPGA XDC 和 nRF 独立
  CH0 overlay。
- 若约 1.8 V：禁止直连。需要定向、推挽、非 I2C 自动双向型电平转换：SCK、MOSI、
  CS_N、SYNC_IN（以及未来 RESET_N）为 3.3→1.8 V，共 5 路；MISO、DRDY 为
  1.8→3.3 V，共 2 路。器件和布线需满足首次 1 MHz，并为目标 8 MHz 留裕量。

### 11.4 SPIS/EasyDMA 与两事务契约审计

- SPIS00 支持 mode 0..3、MSB/LSB first；本项目可按建议冻结首测为 mode 0、MSB first、
  1 MHz，上限仍为 nRF SPIS 规格的 8 MHz。
- 本机 DTS 对 `spi00` 给出 16-bit EasyDMA MAXCNT；8-byte request 和单缓冲 260-byte
  response 均远小于 65535 bytes。Zephyr `spi_nrfx_spis` 不支持 scatter/gather，多段内容
  必须先组装为一个连续 RAM buffer，这与当前 packed 260-byte response 相容。
- 驱动在一次 `spi_transceive()` 前调用 `nrfx_spis_buffers_set()`，并等待
  `NRFX_SPIS_XFER_DONE`；一次 CS transaction 完成后 CPU 可以解析 8 bytes、重装 260-byte
  缓冲，再等待第二次 CS。因此“两次独立 CS”在硬件和驱动模型上可行。
- 但 transaction B 不能紧跟 transaction A 而不给 nRF 软件重装缓冲的时间。当前协议没有
  独立 response-ready 握手，DRDY 又被定义为“record 可 PEEK”的队列电平，不能覆盖
  queue-empty 的 GET_STATUS。故还必须冻结 **A 与 B 之间的最小间隔**；首轮建议 FPGA
  暂用 1 ms，并由逻辑分析仪验证 nRF 已在 B 前 arm response。1 ms 是 bring-up 参数，
  不是当前已证明的最坏情况保证，后续只能根据实测收紧。
- 两个 CS 各自必须覆盖完整 8 或 260 bytes；不得把 request 拆分。Nordic 电气下限仍为
  CS_N→首个 SCK >=1 us、最后 SCK→CS_N >=1 us、CS inactive >=300 ns。首轮 FPGA
  需要显式满足这些值。
- 两事务方案不需要 turnaround/dummy byte；transaction A 同时读出的 MISO 内容忽略，
  transaction B 的 MOSI 固定发 0。若 FPGA 无法保证重装间隔，最小协议调整不是改变
  PEEK/COMMIT，而是增加 response-ready 握手或可检测的 BUSY/RETRY 状态。

现有 command engine 与拟议“完整接口契约”仍有两个明确缺口，真实 backend 实现时必须
显式处理，不能静默假设：

1. 8-byte request 当前没有 request CRC；260-byte response 也没有整体 envelope CRC。
   只有 record 内部 header CRC16（seed 0xFFFF，覆盖 record bytes 0..37）和 payload
   CRC32（覆盖 record bytes 40..243 的 204-byte RF frame）。
2. `GET_INFO`/`GET_STATUS` 当前只返回零初始化 260-byte response 和 status=0，没有定义
   专用 info/status payload。response offsets 0..11 已占 status/transport_seq/record_valid/
   reserved，不能在没有版本化定义时自行塞入状态结构。

因此当前可以冻结的线协议仍是 little-endian request8/response260、command code 1..11、
PEEK/READ 不释放、匹配 transport_seq 的 COMMIT 才释放；GET_INFO/GET_STATUS payload、
request/envelope CRC 策略和 transaction A→B 最坏响应时间仍为未冻结项。

### 11.5 DRDY、SYNC_IN、RESET_N 结论

- DRDY：64-record queue 和现有 pending-record ownership 可以实现高有效电平语义。
  `queue/pending 非空且 PEEK response 可返回` 时保持高；PEEK 后不变；正确 COMMIT 后若
  仍有记录则保持高，最后一条被正确 COMMIT 后拉低；错误/重复 COMMIT 不改变队首或
  DRDY。FPGA 必须用自身 request-in-flight/等待 response 状态门控，不能在 DRDY 持续高时
  无限制重复发 PEEK。尚未实现、未上板验证。
- SYNC_IN：P1.09 支持 GPIOTE20 IN event，可经 DPPI 直接触发 TIMER CAPTURE，满足“不是
  GPIO ISR 软件时间戳”的架构要求。上升沿、空闲低、首次高脉冲 1 us 是联调建议；Nordic
  已检索资料未给出 GPIOTE 最小可保证脉宽，因此 silicon 保证值仍为 UNKNOWN，1 us 必须
  用信号源/逻辑分析仪实测。首轮重复间隔暂取 >=1 ms，且下一脉冲前必须恢复低电平；最终
  最小间隔要由实现资源和硬件测试冻结。
- RESET_N：确认低有效、SoC 内部上拉、Connect Kit R17=1 kΩ 串联且与 IFMCU 共享；但
  nRF54L15 资料检索未得到明确最小低脉宽，也未得到本板允许 FPGA 推挽驱动或必须开漏的
  明确结论。最小脉宽=UNKNOWN，外部驱动类型=UNKNOWN，首轮继续不实现、不接线。

### 11.6 本轮变更、验证边界和准确下一步

- 本轮除本 `handoff.md` 外未修改源代码、Kconfig、devicetree、pinctrl 或测试；未创建
  CH0 overlay，也未替换 `spis_backend_init() == -ENOTSUP`。
- 因没有代码变更，本轮没有把旧 build 结果伪装成新构建；第 3 节已有的软件 build/test
  证据继续有效。真实 SPI、DRDY、GPIOTE capture、10,000 records 和 2 h 都尚未验证。
- Gate 当前被两项用户实物信息阻塞：RX0 PCB revision 丝印/照片和 J4-40 对 J4-39 的
  VDD_GPIO 实测值。收到这两项后，若电压约 3.3 V，再进入独立 CH0 overlay、真实 SPIS、
  level DRDY、硬件 SYNC capture 和软件 clean build；若约 1.8 V，则先冻结七线电平转换。

用户下一条只需回传：`RX0 板卡 revision（附照片）` 和 `VDD_GPIO = x.xx V`。此时仍不要
连接 FPGA 信号线，RESET_N 保持不接。

## 12. 2026-08-18 CH0 SPIS 软件实现与 3.3 V gate 更新

本节替代 11.6 中“尚未实现”的状态。用户已实测并确认 Connect Kit 的
`VDD_GPIO` 约为 3.3 V。结合已确认的 ZYNQ Bank13 `VCCO=3.3 V` 和拟用
`LVCMOS33`，七线接口的电压兼容性评审 gate 已通过；这不是通电直连、波形或数据事务
成功的证据。RX0 实物 PCB revision 丝印仍需在首次接线前目视确认是 Rev.A。

### 12.1 已实现的 CH0 软件范围

- 新增独立 `applications/rf_link_rx/ch0_spis.conf` 和 `ch0_spis.overlay`；默认无线、
  stream 和 future/debug 配置不受该 overlay 污染。
- SPIS00 使用 SCK=P2.01/J4-9、SDI=P2.04/J4-12、SDO=P2.02/J4-10、
  CSN=P2.05/J4-13；生成 DTS 已核对为 `nordic,nrf-spis`。
- 新增真实 Zephyr/nrfx SPIS backend：先接收一个完整 8-byte request CS transaction，
  解析后重装一个连续 RAM 中的 260-byte response，再等待第二个完整 CS transaction。
- DRDY=P1.10/J4-31，高有效、保持型；pending record 存在时为高，重复 PEEK 不改变，
  匹配 sequence 的 COMMIT/DROP 后才释放；释放后若队列仍有记录，会先 stage 下一条再
  更新 DRDY，避免队列未空时出现错误低脉冲。
- SYNC_IN=P1.09/J4-30，上升沿进入 GPIOTE20 event，并由 GPPI/DPPI 直接触发
  TIMER20 CC1 capture；ISR 只读取已经硬件捕获的 CC 值并送入 sync manager。TIMER20
  运行于 1 MHz、32-bit，CC0 用于同一 timebase 的当前时间读取。这不是普通 GPIO ISR
  软件时间戳。
- GET_INFO/GET_STATUS 现在返回版本化 payload；新增 SPIS request/response、SPI error、
  parser error 和 short-transfer 统计。原有 RF、reorder、queue 和 248-byte record 格式
  未改动。

### 12.2 冻结的 CH0 事务契约 v1

- SPI slave：mode 0（CPOL=0、CPHA=0）、MSB first；首次 SCLK=1 MHz，nRF 能力上限
  8 MHz，但未做板级信号完整性验证前不得升频。
- transaction A：CS_N 持续有效覆盖 8-byte request。offset0=`command u8`，offset1..3
  必须为 0，offset4..7=`argument u32 little-endian`。
- A/B 间隔：首次固定至少 1 ms，给 nRF 解析 request 并重装 EasyDMA response；这是
  bring-up 参数，不是已经测得的最坏情况保证。
- transaction B：CS_N 持续有效覆盖 260 个字节；MOSI 发 0，MISO 返回 response；无
  turnaround/dummy byte。offset0=`status int32 LE`，offset4=`transport_seq u32 LE`，
  offset8=`record_valid u8`，offset9..11=0，offset12..259=248-byte payload。
- 每次 CS：CS_N 到首个 SCK >=1 us，最后 SCK 到 CS_N >=1 us，CS inactive >=300 ns。
- command 1..11 保持原定义。PEEK/READ 不释放；COMMIT/DROP 的 argument 必须匹配
  pending `transport_seq`；错误或重复 COMMIT 不释放队首。
- request 和整个 response envelope **没有 CRC**。PEEK/READ record 内部仍有 header
  CRC16（seed 0xFFFF，record bytes0..37）和 payload CRC32（record bytes40..243）。
  这是现有代码/测试契约，与“整包 SPI CRC”不是同一概念。
- GET_INFO payload 从 response offset12 开始，28 bytes：magic `0x3149464E`、contract
  version、capabilities、request/response/record sizes、mode、bit order、8 MHz 上限、
  1 MHz 初始值和 1000 us A/B 间隔。
- GET_STATUS payload 从 response offset12 开始，56 bytes：magic `0x3153464E`、version、
  sync state、queue level/high-water、sync epoch/tick、pending sequence、CRC/invalid/
  duplicate commit/SPI/parser/short-transfer counters。

### 12.3 构建与测试证据

以下均在分支 `codex/rf-link-2p4g-devlog`、HEAD
`4e360863a3569c9494197ec701604404519d811d` 的 dirty 工作树上执行，未 reset、checkout、
clean 或覆盖既有修改：

- 默认 RX clean build：`west build -p always -d build_codex_ch0_rx_regression -b
  nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx`，0 error；Flash 62,596 B，
  RAM 33,144 B。
- future/debug clean build：`west build -p always -d build_codex_ch0_future_regression -b
  nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx --
  "-DEXTRA_CONF_FILE=future.conf"`，0 error；Flash 69,192 B，RAM 48,984 B。
- CH0 SPIS clean build：`west build -p always -d build_codex_ch0_spis -b
  nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx --
  "-DEXTRA_CONF_FILE=ch0_spis.conf" "-DEXTRA_DTC_OVERLAY_FILE=ch0_spis.overlay"`，
  0 error；Flash 79,640 B，RAM 53,400 B。Zephyr 明确警告 SPI slave API 为 experimental，
  不等于构建失败。
- Python contract/fault tests：`python -m unittest discover -s tests -p
  test_rf_link_future.py -v`，13/13 PASS。
- 生产 C 模块 ztest：`west build -p always -d build_codex_rf_link_future_c_ch0 -b
  qemu_cortex_m3 tests\rf_link_future_c`，编译 0 error；Flash 34,716 B，RAM 25,568 B。
  本机没有 QEMU executable（`QEMU-NOTFOUND`），所以该 ztest **没有实际执行**，不能写成
  runtime PASS。
- `git diff --check` 通过。以上全是软件证据；本轮没有烧录 CH0 SPIS 映像，也没有
  SPI/DRDY/SYNC 逻辑分析仪或 ZYNQ 事务证据。

### 12.4 下一步准确停点：断电接线与首次上电

1. 目视确认 RX0 PCB 丝印为 Connect Kit Rev.A；若不是，停止并重新审计。
2. 两板断电。先仅连公共地：nRF J4-39 GND 到 FPGA J4-37 或 J4-39 GND。
3. 仍在断电状态连接：FPGA J4-12 SCK -> nRF J4-9；FPGA J4-4 MOSI -> nRF J4-12；
   FPGA J4-6 MISO <- nRF J4-10；FPGA J4-8 CS_N -> nRF J4-13；FPGA J4-10 DRDY <-
   nRF J4-31；FPGA J4-14 SYNC_IN -> nRF J4-30。
4. RESET_N 两侧均保持不接。不要连接 FPGA J4-38 的 3.3 V、J4-40 的 5 V，也不要用
   nRF J4-40 VDD_GPIO 给对方供电。
5. 逻辑分析仪共地并同时观察 CS_N、SCK、MOSI、MISO、DRDY、SYNC_IN。
6. 当前普通 PowerShell 的 `pyocd` 不在 PATH；使用仓库 west 环境中的明确入口烧录：
   `& 'D:\nRF54L15\NCS-Project\.venv\Scripts\pyocd.exe' load -u
   820D9A5F0F3BA22B8E4ES -t nrf54l build_codex_ch0_spis\merged.hex`。该入口的
   `list` 已识别 RX probe，但因 Windows GBK 无法打印 Unicode checkmark 而以 code 1
   结束；这不构成烧录验证。烧录后先保存完整启动日志，烧录成功也不能代替 SPI 成功。
7. ZYNQ 首次设置 mode0、MSB-first、1 MHz、A/B 间隔至少1 ms，先 GET_INFO，再
   GET_STATUS；随后按重复 PEEK、正确 COMMIT、错误/重复 COMMIT、SYNC timestamp 顺序。

硬件验收仍全部未完成：实际 GET_INFO/GET_STATUS、DRDY 电平保持、PEEK/COMMIT 波形、
SYNC capture、故障注入、10,000 records、2 h 长测。下一次需要用户回传 Rev.A 丝印确认、
接线照片、nRF 启动日志、ZYNQ 事务日志和逻辑分析仪截图/导出文件。

## 13. 2026-09-03 FPGA实板header CRC16故障修正

用户报告 FPGA 实板已经确认 GET_INFO、GET_STATUS、ARM_SYNC、START_STREAM 和 PEEK
可以运行，但所有真实248-byte record均在FPGA header CRC16检查失败。审计确认
`fpga_transport.c` 的record生成路径和 `fpga_spi_transport.c` 的stage校验路径均错误使用
Zephyr `crc16_ccitt(0xffff, ...)` reflected算法，与冻结的CRC-16/CCITT-FALSE不一致。

本轮只改变这一主要变量：

- 两处统一改为 `crc16(0x1021u, 0xffffu, data, length)`，长度严格为
  `offsetof(struct fpga_record_header, header_crc16)`，即record header bytes0..37。
- payload CRC32、record布局、request8/response260、SPI两事务、RF参数和FPGA端均未修改。
- 生产C ztest新增标准向量：`CRC16-CCITT-FALSE("123456789") == 0x29B1`。
- 生产C ztest对 `fpga_transport_build_record()` 生成的真实header bytes0..37重新计算，
  并与 `header_crc16` 字段直接比较。
- Python模型函数明确命名为 `crc16_ccitt_false`，并增加同一 `0x29B1` 标准向量。

软件验证：

- CH0 SPIS强制clean build：`west build -p always -d build_codex_ch0_spis_crcfix -b
  nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx --
  "-DEXTRA_CONF_FILE=ch0_spis.conf" "-DEXTRA_DTC_OVERLAY_FILE=ch0_spis.overlay"`：
  0 error；Flash 79,672 B，RAM 53,400 B；输出
  `build_codex_ch0_spis_crcfix\merged.hex`。
- 生产C ztest强制clean build：`west build -p always -d
  build_codex_rf_link_future_c_crcfix -b qemu_cortex_m3 tests\rf_link_future_c`：
  编译/链接0 error；Flash 35,216 B，RAM 25,584 B。
- C ztest仍未实际执行：QEMU命令为 `QEMU-NOTFOUND`；另尝试native_sim，但本机缺少
  host `gcc`，在CMake配置阶段停止。这是测试环境限制，不可写成C runtime PASS。
- `python -m unittest discover -s tests -p test_rf_link_future.py -v`：14/14 PASS，
  包括新增的CCITT-FALSE标准向量和既有header/payload损坏检测。

用户随后明确要求设备先断电，本轮只编译。因此没有连接/复位/烧录RX0，也没有采集新
COM11日志。准确下一步是在用户确认可以上电后烧录
`build_codex_ch0_spis_crcfix\merged.hex`，保存COM11启动日志，再由FPGA重复PEEK并核对
header CRC16、payload CRC32与COMMIT闭环。烧录成功不能代替FPGA实板CRC通过。

## 14. 2026-09-03 CRC修正版烧录与COM11启动证据

用户确认设备重新上电后，仅对RX0执行了以下操作；TX和FPGA固件未修改：

- `pyocd list` 同时识别两块Connect Kit；RX0 probe为
  `820D9A5F0F3BA22B8E4ES`，COM11存在。
- 烧录命令：`& 'D:\nRF54L15\NCS-Project\.venv\Scripts\pyocd.exe' load -u
  820D9A5F0F3BA22B8E4ES -t nrf54l build_codex_ch0_spis_crcfix\merged.hex`。
- pyOCD返回0：erase 81,920 bytes、program 81,920 bytes、skip 0 bytes；映像SHA256为
  `559E07A5508AFE37C171AE03010826917E890B8C2C33E88701CBF0D24AA152F5`。
- 连接COM11 115200 8N1后主动reset RX0，捕获到完整启动标识：

```text
rf_link_rx started
mode=ESB_PRX,phy=4M,ack=noack,channel=40,pipe=0,prefix=0x54,fw=v0.5-rf-throughput-optimization-8-g4e360863a356,samples_per_frame=96,v2_preview=1,node_id=0
radio ready, waiting packets
```

未出现 `timebase init failed` 或 `FPGA transport init failed`。reset后短时日志从
`RADIO events=127` 增长到至少1536；最后记录为：

```text
RADIO events=1536 frames=1536 read_errors=0 queue_overflow=0 queue_high_water=0
REORDER extended_frame_seq=18145 rf_lost_frames=12 rf_lost_samples=1152 rf_duplicates=0 rf_late_frames=0 index_estimated=11 bad_magic=0 bad_size=0 bad_sample_count=0
SYNC state=0 sync_capture_count=0 sync_timeout=0 sync_epoch=0 sync_locked=0 resync_count=0
QUEUE push_total=0 pop_total=0 overflow=0 high_water=0 level=0
SPI_TRANSPORT records=0 crc_errors=0 invalid_cmd=0 duplicate_commit=0 submit_errors=0 stall_ms=0 req_xfer=0 rsp_xfer=0 spi_errors=0 parser_errors=0 short_xfer=0
CONTROL armed=0 started=0 stopped=0 reset=0 invalid=0
```

这证明CRC修正版已下载、RX无线接收和CH0初始化可运行，但不证明FPGA header CRC已修复。
当前FPGA没有在RX reset后发起事务：`req_xfer=0`、`started=0`，所以queue保持空且没有真实
record可复验。若FPGA测试bitstream仅通过JTAG配置，整机断电后必须重新Program Device。
下一步由用户重新下载/启动FPGA CH0测试bitstream，再回传FPGA控制台或ILA中至少一次
START_STREAM、PEEK及header CRC PASS/FAIL，同时采集对应COM11的req/rsp/record计数。

## 15. 2026-09-03 组合工作区与论文写作交接

本轮只更新nRF仓库的工程文档，没有移动源码、修改固件代码、重新构建或操作硬件。当前
分支仍为`codex/rf-link-2p4g-devlog`，HEAD仍为
`4e360863a3569c9494197ec701604404519d811d`；工作树继续保留此前全部已跟踪修改和未跟踪
CH0文件，严禁用reset、checkout或clean丢弃。

为方便三个工程切换对话和后续论文整理，新增以下稳定入口：

- `docs/PROJECT_CONTEXT.md`：当前CH0目标、硬件身份、冻结契约、已验证边界、构建入口和
  新对话首条指令；
- `docs/WORKSPACE_MIGRATION.md`：推荐VS Code多根工作区、不搬动源码的方案，以及必须
  物理迁移时的完整仓库边界和排除项；
- `docs/PAPER_EVIDENCE_INDEX.md`：论文可引用结果、只能称为软件验证的结果、未验证事项和
  原始证据元数据要求；
- `mkdocs.yml`：把上述三份文档加入`Project Integration`导航；
- 根`README.md`、`applications/rf_link_README.md`、DESIGN、ROADMAP、
  FUTURE_INTEGRATION和INTEGRATION_REPORT均已指向当前CH0状态，旧阶段性结论保留为历史
  记录但由较新章节覆盖。

最简单且稳定的集成方式是：在新的系统级目录创建一个`.code-workspace`，分别引用现有
`D:\nRF54L15\NCS-Project\nrf54l15-connectkit`、FPGA工程、第三个工程和新建的`paper/`
目录。nRF源码无需复制；West根目录的`.west`、NCS 3.1.0模块和`.venv`继续留在原位置。
不要复制`build*`、缓存或整套NCS依赖。若以后必须物理迁移，最小可靠单位是整个dirty
的`nrf54l15-connectkit`工作树（包含`.git`和未跟踪CH0文件），不能只clone当前HEAD。

切换到新对话时先读`docs/PROJECT_CONTEXT.md`，再读本文件第13至15节并检查
`git status --short --branch`。当前准确的硬件下一步没有改变：重新Program FPGA CH0
测试bitstream，然后复验CRC修正版的START_STREAM/PEEK/COMMIT闭环并同步保存FPGA、
COM11和逻辑分析仪证据。

## 16. 2026-09-03 用户选择物理迁移后的精简边界

用户明确需要在新组合工作区创建新对话，因此选择物理复制nRF工程。本轮只读体积审计
确认：当前`nrf54l15-connectkit`约1108.45 MiB；根目录40个`build`/`build_*`约
1010.02 MiB；复制后只删除这些构建目录可将副本降到约98.43 MiB，并保留完整源码、
板定义、文档、Git历史和dirty工作树。

准确迁移单位仍是整个`nrf54l15-connectkit/`，包含隐藏的`.git/`和所有未跟踪CH0文件。
不得只clone HEAD，也不建议删减`applications/`、`boards/`、`docs/`、`tests/`、`scripts/`
或根构建配置。不要使用`git clean -fdX`，因为其范围包含除build之外的其他ignored文件。
安全预览、删除模板和搬出West workspace后的显式`BOARD_ROOT`构建命令已写入
`docs/WORKSPACE_MIGRATION.md`。

本轮没有实际复制或删除任何目录。迁移后的验证顺序为：新旧`git status`逐行一致、只在
新副本删除build目录、从原NCS 3.1.0环境对新路径执行CH0 clean build。只有该构建成功后，
才能把新副本作为后续nRF开发主目录；原目录应先保留为回退副本，不要立即删除。

## 17. 2026-09-03 新工作区junction与clean build验证

nRF源码已迁移到`F:\OliverS\MultiSensorResearch\nrf54l15-connectkit`，Git分支、HEAD和
dirty源码均保留。West 1.5.0从D:根目录直接接收F:源码路径时会在`os.path.relpath()`处
报不同盘符错误，因此在NCS workspace内建立：

```text
D:\nRF54L15\NCS-Project\MultiSensorResearch-nrf
  -> F:\OliverS\MultiSensorResearch\nrf54l15-connectkit
```

同时把West `manifest.path`切换为`MultiSensorResearch-nrf`，避免D:旧仓库与F:新仓库
重复定义`nrf54l15_connectkit` board。随后从junction路径对CH0 SPIS执行`-p always`
clean build，编译和链接0 error；Flash 79,672 B、RAM 53,400 B。输出：

```text
build_workspace_ch0_spis/merged.hex
SHA-256 559E07A5508AFE37C171AE03010826917E890B8C2C33E88701CBF0D24AA152F5
```

构建日志明确显示板级DTS来自F:当前源码。实验性`SPI_SLAVE`、空console library与全局
assert为配置警告，不是构建失败。构建目录被现有`/build*`规则忽略，构建前后Git dirty
条目均为41。该结果证明迁移后的当前源码可离线构建，不代替烧录或CH0实板验收。

## 18. 2026-09-03 `submit_errors`并发根因与软件修复

用户实板日志出现`records=1957`、`submit_errors=49`，同时frame queue已有大量overflow。
源码审计确认`fpga_transport_service()`有两个不同线程入口：RX主线程在收帧和主循环中
调用，SPIS worker在COMMIT/DROP成功后也会立即调用。原实现只用`transport_lock`保护统计
字段，没有保护完整的“peek队首 -> 检查pending -> build -> submit”事务。两个线程可同时
peek同一队首并都观察到无pending；先进入者stage成功，后进入者从backend得到`-EBUSY`，
随后被笼统计入`submit_errors`。这也会并发读写全局`pending_extended_frame_seq`。

修复在`fpga_transport.c`增加Zephyr mutex，对整个`fpga_transport_service()`事务串行化，
所有返回路径均在统一出口解锁。没有删除SPIS worker的服务调用，因此正确COMMIT后仍会
立即stage下一条记录，不引入等待主循环下一轮的额外延迟。原有spinlock继续只承担短时
统计/序号字段保护；无线参数、队列深度、record格式、SPI线协议和DRDY语义均未改变。

新增C ztest以两个同优先级线程同时服务64条记录，验收队列清空、总处理数64、records=64
且`submit_errors=0`；Python source-contract也固定mutex定义和入口加锁。软件侧验证结果：

- `python -m unittest tests.test_rf_link_future -v`：14/14 PASS；
- `qemu_cortex_m3`生产C ztest clean build：139步完成，Flash 38,764 B、RAM 28,008 B；
- C ztest runtime仍未执行，运行目标明确失败于本机`QEMU-NOTFOUND`；
- CH0 SPIS `-p always` clean build：275步完成，Flash 79,708 B、RAM 53,416 B；
- 新`merged.hex` SHA-256：
  `82B2FF7D32666E2D828A364FBC6191A612C4E175956634F3C7EC731BB68F5291`；
- `git diff --check`通过。

以上证明修复已通过源码合约、C交叉编译和真实CH0配置编译，不证明实板竞态已经消失。
下一步需将上述新镜像烧录RX0，在相同FPGA流量和统计窗口下复测：`submit_errors`应保持0，
`records`与`pop_total`持续推进；同时分别观察`spi_errors/parser_errors/short_xfer`，避免把
真实物理层错误误归因于本次并发问题。queue overflow是吞吐/背压问题，不能因为
`submit_errors`清零就视为一并解决。
