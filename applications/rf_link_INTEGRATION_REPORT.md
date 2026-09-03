# MEMS 与私有 2.4G 工程整合报告

> 状态说明（2026-09-03）：本文主体记录最初MEMS/无线合并过程，部分“尚未上板”描述是
> 历史结论。当前CH0无线与ZYNQ SPIS进展请优先读取
> [`../docs/PROJECT_CONTEXT.md`](../docs/PROJECT_CONTEXT.md)和
> [`../handoff.md`](../handoff.md)最后两节；论文证据边界见
> [`../docs/PAPER_EVIDENCE_INDEX.md`](../docs/PAPER_EVIDENCE_INDEX.md)。

## 整合基线

- 主工程：`YoungsenseSong/nRF54L15-2.4G`，基线提交 `f635103`。
- 参考工程：`Ins1ghtt/2.4g-mems-`，审计提交 `371be76`。
- SDK：nRF Connect SDK 3.1.0，Zephyr 4.1.99。
- 原则：无线协议、ESB 参数、RX 行为及主仓文档历史均以主工程为准；只移植并重构参考工程中的 IIM-42352 采集能力。

两个仓库没有可用的共同 Git 历史，参考工程是上传式重建，因此本次采用逐文件审计和人工移植，没有执行整仓 merge。

## 最终 TX 数据通路

```text
IIM-42352 @ 4 kHz/axis
  -> LP/FLPR 通过 SPIM00 读取 2 KB FIFO
  -> XYZ 交错写入共享 SRAM 双缓冲
  -> 每槽累计 4096 个 int16_t 标量样本
  -> CRC32 + ICMsg “batch ready” 描述符
  -> VEVIF 中断唤醒处于 System ON idle 的 HP/CPUAPP
  -> HP 校验共享槽头与 CRC32
  -> 42 个 96-sample 帧 + 1 个 64-sample 帧
  -> 主仓 ESB 4 Mbps/no-ACK/204-byte 帧链路
  -> HP 通过 ICMsg 回传 “batch release”
  -> LP 复用对应共享槽
```

这里的“HP 睡眠”是 Zephyr 主线程阻塞后由内核进入的 System ON idle，不是 System OFF。ICMsg 使用 VEVIF mailbox 中断唤醒 CPUAPP。无线模块在第一批数据到达时才延迟初始化，避免启动后立即占用发送路径。

4096 的定义是 4096 个 `int16_t` 标量，顺序为 `X0,Y0,Z0,X1,Y1,Z1,...`。IIM-42352 为 4 kHz 三轴采样，因此聚合标量速率为 12 ksps，理论有效载荷为 192 kbit/s，一批约 341.3 ms。

## 共享内存布局

| 地址 | 大小 | 用途 |
| --- | ---: | --- |
| `0x20018000` | `0x800` | ICMsg LP -> HP |
| `0x20020000` | `0x800` | ICMsg HP -> LP |
| `0x20020800` | `0x100` | LP 运行诊断 |
| `0x20021000` | `0x2100` | MEMS 共享槽 0 |
| `0x20023100` | `0x2100` | MEMS 共享槽 1 |
| `0x20028000` | `0x18000` | FLPR 代码与运行 SRAM |

CPUAPP 有数据缓存，读取共享批次前执行 cache invalidate；FLPR 发布描述符前执行内存屏障。每个槽由双向 ready/release 消息显式交接所有权，HP 未释放前 LP 不会覆盖。

## 接受并重构的参考工程内容

- IIM-42352 使用 SPI mode 3，8 MHz。
- SPIM00 引脚：SCK `P2.01`、MOSI `P2.02`、MISO `P2.04`、CS `P2.00`。
- INT1 使用 `P0.02`，低有效下降沿。
- Packet 1 FIFO 格式：8 字节，包含头、XYZ 三轴和温度字节。
- 加速度计低噪声模式、4 kHz ODR、正负 16 g、FIFO 半满水位。

采集代码被重写为“GPIO ISR 只发信号，LP 主线程读取和解析 FIFO”，避免参考工程中系统 workqueue 与主线程并发访问无锁环形缓冲。

## 冲突与处理结果

| 冲突 | 参考工程行为 | 本次处理 |
| --- | --- | --- |
| HP 输出路径 | 每个 IPC 帧先通过 UART 输出二进制，再无线发送 | 不合入。串口轮询会占用实时发送窗口，最终数据出口保持主仓 ESB；UART 只输出低频诊断。 |
| 4096 批次 | 仅有 384 个 `int16_t` 环形缓存，仍按 96 样本逐帧 IPC | 重写为两个 4096 样本共享槽，小描述符通知，HP 读取后分片无线发送。 |
| FIFO 水位/读取长度 | 水位为 1024 字节，但一次最多读取 256 字节 | 一次读取当前完整 FIFO，最大缓冲按数据手册建议覆盖 2080 字节。 |
| FIFO 计数顺序 | 先读 `FIFO_COUNTH` 再读 `FIFO_COUNTL` | 改为先读低字节以锁存两字节，再读高字节。 |
| 4 kHz 中断脉宽 | 使用默认 100 us | 设置 `INT_CONFIG1.INT_TPULSE_DURATION=1`，按数据手册在 ODR >= 4 kHz 时使用 8 us。 |
| FIFO 包校验 | 只检查头 bit 6 | 按 Packet 1 的 `0100_00xx` 使用 `0xFC` 掩码校验。 |
| RX 配置 | 删除 `CONFIG_NRFX_DPPI10` | 不合入，保留主仓经过验证的 RX 配置。 |
| Board Kconfig | 修改蓝牙控制器默认项等全局板级配置 | 不合入，与本次 MEMS TX 无关且可能影响其他样例。 |
| 大型二进制/认证附件 | 参考仓因重新上传产生大量无关二进制差异 | 全部忽略。 |
| `P0.02` | Connect Kit 原板级 DTS 同时把它定义为 LED0 | FLPR overlay 中禁用 LED0，把该脚交给 MEMS INT1；上板前必须确认实际 PCB/飞线确实如此。 |

## 离线验证结果

无需连接开发板，以下构建已在 NCS 3.1.0 下通过：

```powershell
west build -p always --sysbuild -d build_rf_link_tx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_tx

west build -p always -d build_rf_link_rx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx
```

- FLPR：RAM 约 58.3 KB / 96 KB。
- TX CPUAPP：RAM 约 25.1 KB / 160 KB。
- RX CPUAPP：RAM 约 19.2 KB / 188 KB。
- `git diff --check` 的代码部分无空白错误。

上述尺寸是最初整合阶段的离线结果，不再代表当前CH0构建尺寸。后续硬件已经完成
30分钟q64无线基线，实测44帧丢失、radio queue overflow=0；ZYNQ控制面已走到PEEK。
CRC-16/CCITT-FALSE修正版RX已经clean build并烧录，但FPGA断电后尚未重新Program复验
record header CRC。真实功耗、10,000 records和2小时SPI长稳仍未完成。

## 接板后的测试步骤

### 1. 接线复核

| IIM-42352 | nRF54L15 |
| --- | --- |
| SCLK | P2.01 |
| SDI/MOSI | P2.02 |
| SDO/MISO | P2.04 |
| CS | P2.00 |
| INT1 | P0.02 |
| VDD/VDDIO/GND | 按硬件电压和原理图连接 |

特别确认 `P0.02` 没有仍被板载 LED 或其他电路强驱动。

### 2. 烧录

先用 `pyocd list` 确认 TX/RX 两块板的序列号。TX 必须烧录两个镜像：

```powershell
pyocd load -u <TX_ID> -t nrf54l build_rf_link_tx_mems\rf_link_tx\zephyr\zephyr.hex
pyocd load -u <TX_ID> -t nrf54l build_rf_link_tx_mems\flpr_app\zephyr\zephyr.hex
pyocd load -u <RX_ID> -t nrf54l build_rf_link_rx_mems\rf_link_rx\zephyr\zephyr.hex
```

### 3. 先验证 TX 采集与核间链路

TX 启动后应先打印：

```text
LP IPC bound; CPUAPP blocking until a 4096-sample batch is ready
```

第一批到达后才应打印 `radio ready after first MEMS batch`。随后约每秒出现一次 `TX stat`。健康状态应满足：

- `batches_rx`、`batches_ok` 持续增长，约每秒 2.9 批。
- `frames_ok` 每批增长 43。
- `samples_ok` 每批增长 4096。
- `batch_bad_hdr=0`、`batch_bad_crc=0`、`ipc_drop=0`。
- `lp_malformed=0`、`lp_io_err=0`、`lp_fifo_ovf=0`。
- `lp_slot_wait` 理想为 0；持续增加表示 HP/无线发送速度低于采集速度。
- `lp_poll` 偶发增加表示 GPIO 边沿未捕获但轮询已恢复；持续增加需检查 INT1 接线、极性和 GPIOTE。

若 LP 初始化失败，HP 状态中的 `lp_fatal` 和 `lp_fatal_reason` 会记录错误。优先检查 WHO_AM_I、SPI mode 3、供电和 CS。

### 4. 验证无线 RX

默认 RX 串口应看到：

```text
RX stat frames=... samples=... bps=... lost=... dup=... bad=...
```

稳定运行时：

- `samples` 平均每秒约增加 12000。
- `bps` 长期平均接近 192000 bit/s；由于 TX 是批量突发，单个一秒窗口会有波动。
- `bad=0`、`dup=0`，`lost` 不应持续快速增加。

### 5. 验证每批恰好 4096 样本

构建 RX 二进制流版本并抓取至少 200 帧：

```powershell
west build -p always -d build_rf_link_rx_stream `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=stream.conf" "-DEXTRA_DTC_OVERLAY_FILE=stream.overlay"

python .\dump_rx_frames.py --port COM7 --max-frames 220 `
  --output .\2.4g_results\mems_batch_test.csv

python .\verify_mems_batches.py .\2.4g_results\mems_batch_test.csv
```

最后一条命令成功时退出码为 0，并应报告 `bad_batches=0`、`incomplete_batches=0`、`sequence_gaps=0`。每个完整批次应为 43 个 RF 帧，总样本数正好 4096。

### 6. 长稳与功耗

建议至少连续运行 30 分钟，并同时保存 TX/RX 状态：

```powershell
python .\save_serial_csv.py --port <TX_COM> --output tx_mems_stats.csv
python .\save_serial_csv.py --port <RX_COM> --output rx_mems_stats.csv
```

重点观察 CRC、FIFO overflow、slot wait、RF timeout 和 RX lost 是否随时间增长。功耗测试应分别测量 HP 等待批次和 HP 批量发射两个阶段；当前实现验证的是 System ON idle 唤醒，不宣称 System OFF 功耗。

## 参考资料

- [TDK IIM-42352 数据手册](https://product.tdk.com/system/files/dam/doc/product/sensor/mortion-inertial/accelero/data_sheet/ds-000442-iim-42352-typ-v1.2.pdf)
- [主工程 YoungsenseSong/nRF54L15-2.4G](https://github.com/YoungsenseSong/nRF54L15-2.4G)
- [参考工程 Ins1ghtt/2.4g-mems-](https://github.com/Ins1ghtt/2.4g-mems-)
