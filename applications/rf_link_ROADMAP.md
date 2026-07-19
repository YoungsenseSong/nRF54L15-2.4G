# nRF54L15 无线采集工程 V1-V6 路线图

## 1. 版本口径

本仓库从本次同步起统一使用下面的版本口径，避免“Future V1”和现有 V1 混淆：

| 工程版本 | 定位 | 硬件范围 |
| --- | --- | --- |
| V1 | 当前单路成熟基线：MEMS、TX 双核、204 字节空中帧、RX 统计与抓帧 | 1 块 TX + 1 块 RX |
| V2 | 单路 nRF RX 与 ZYNQ 的 SPIS、DRDY、SYNC 硬件联调 | 1 块 TX + 1 块 RX + ZYNQ |
| V3 | 四路独立无线链路汇聚和统一逻辑 epoch | 4 块 TX + 4 块 RX + ZYNQ |
| V4 | 第二代空中协议：节点身份、批次、样本索引和高分辨率时间 | TX/RX 同时升级 |
| V5 | 物理同步采样、漂移测量与校正 | 完整四路系统及同步控制链路 |
| V6 | 长稳、故障恢复、性能功耗和可交付工程化 | 定型硬件 |

当前仓库中的 `rf_link_rx/future.conf` 是 **V2 软件预集成配置**。其中的接收队列、
逻辑同步状态机和 ZYNQ 记录协议已经实现并可离线测试，但真实 SPIS、DRDY 和
GPIOTE-DPPI-TIMER SYNC 捕获尚未绑定硬件，因此不计入 V1 上板验收，也不代表
V2 已完成。

## 2. V1：两块 nRF 的上板测试

### 2.1 测试目标

在引入 ZYNQ、四路并发和新空中协议之前，建立一份可复现的单链路基线，证明：

1. IIM-42352 能由 FLPR 连续采集；
2. 每 4096 个 `int16_t` 样本只发生一次完整的共享内存所有权交接；
3. CPUAPP 能从 System ON idle 唤醒并发送 43 个 RF 帧；
4. RX 能正确接收、统计并导出完整批次；
5. 两块开发板互换角色后，结论仍然成立。

V1 只需要两块 nRF54L15 Connect Kit：一块烧录 TX 的 CPUAPP + FLPR 两个镜像，
另一块烧录 RX 镜像。它不能验证四路并发、ZYNQ SPIS 或物理同步。

### 2.2 离线构建回归

```powershell
west build -p always --sysbuild -d build_rf_link_tx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_tx

west build -p always -d build_rf_link_rx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx

west build -p always -d build_rf_link_rx_stream `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=stream.conf"

python -B -m unittest -v tests.test_rf_link_future
```

构建通过只能说明代码和配置完整，不能替代传感器、无线和功耗测试。

### 2.3 接线和烧录

IIM-42352 接线沿用 V1 定义：SCK `P2.01`、MOSI `P2.02`、MISO `P2.04`、
CS `P2.00`、INT1 `P0.02`。必须先确认 `P0.02` 与板载 LED0 的实际电气冲突已按
硬件连接处理。

```powershell
pyocd list
pyocd load -u <TX_ID> -t nrf54l build_rf_link_tx_mems\rf_link_tx\zephyr\zephyr.hex
pyocd load -u <TX_ID> -t nrf54l build_rf_link_tx_mems\flpr_app\zephyr\zephyr.hex
pyocd load -u <RX_ID> -t nrf54l build_rf_link_rx_mems\rf_link_rx\zephyr\zephyr.hex
```

### 2.4 分级测试与通过条件

| 测试 | 操作 | 通过条件 |
| --- | --- | --- |
| TX 启动 | 观察 CPUAPP 和 FLPR 串口 | IPC 建链；第一批到达后才初始化无线；无 fatal |
| MEMS 批次 | 连续观察至少 20 批 | 每批 `samples_ok +4096`、`frames_ok +43` |
| 共享内存 | 检查 TX 计数器 | `batch_bad_hdr/batch_bad_crc/ipc_drop/release_err` 全为 0 |
| MEMS/FIFO | 检查 FLPR 计数器 | `lp_malformed/lp_io_err/lp_fifo_ovf` 全为 0；持续 `lp_poll` 必须排查 |
| 无线接收 | 默认 RX 连续运行 | `bad=0`、`dup=0`；长期平均约 12 ksps、192 kbit/s |
| 批次导出 | 抓取至少 220 帧 | 解析结果 `bad_batches=0`、`incomplete_batches=0`、`sequence_gaps=0` |
| 角色互换 | 两块板交换 TX/RX 角色后重复 | 两个方向均满足以上条件 |
| 短稳测试 | 连续运行至少 30 分钟 | 无死锁、重启、计数停止或队列持续堆积 |
| 长稳测试 | 推荐连续运行 2 小时以上 | 错误计数不随时间持续增长；记录实际丢包率和环境 |

无线丢包率不设脱离环境的虚假固定门限。测试记录必须同时注明距离、遮挡、供电、
天线方向、PHY、信道和运行时间；在相同条件下建立 V1 基线，后续版本不得明显退化。

### 2.5 数据抓取

```powershell
python .\dump_rx_frames.py --port <RX_COM> --max-frames 220 `
  --output .\2.4g_results\v1_frames.csv

python .\verify_mems_batches.py .\2.4g_results\v1_frames.csv

python .\save_serial_csv.py --port <TX_COM> `
  --output .\2.4g_results\v1_tx_stats.csv

python .\save_serial_csv.py --port <RX_COM> `
  --output .\2.4g_results\v1_rx_stats.csv
```

建议同时保存：固件提交号、两块板序列号与角色、接线照片、测试开始/结束时间、
供电方式、距离和异常现象。V1 只有在上述硬件数据形成记录后才能标记为“上板通过”。

## 3. V2：单路 ZYNQ 接口落地

目标是先把一块 RX nRF 与 ZYNQ 的数据和控制闭环做稳定，不立即扩展四路。

计划内容：

- 与 ZYNQ 端冻结 SPI mode、最大时钟、CS 时序、电平、DRDY 极性和 SYNC 扇出；
- 评审并冻结转接板原理图，再确定 `spi21` 及 P1.4-P1.10 候选引脚；
- 增加独立 devicetree overlay 和 pinctrl，不改变默认 V1 构建；
- 实现真实 nRF SPIS 从机后端和电平型 DRDY；
- 实现 GPIOTE -> DPPI -> TIMER 的 SYNC 硬件捕获；
- 联调 GET/STATUS/PEEK/READ/COMMIT/DROP/ARM/START/STOP/RESET；
- 验证重复读取、错误 COMMIT、CRC 破坏、中断事务、超时和 ZYNQ 重启恢复；
- 完成单路持续吞吐和至少 2 小时稳定性测试。

V2 入口条件是 V1 有硬件基线，并且 ZYNQ 至少具备可运行的 SPI 主机原型。V2
完成标志是一条真实 MEMS 数据链经无线到 RX，再经 SPIS 被 ZYNQ 正确提交和保存。

## 4. V3：四路汇聚与逻辑同步

计划内容：

- 四块 RX 使用唯一 `node_id` 和统一的 `sync_epoch`；
- ZYNQ 同时管理四套 SPIS/DRDY，按逻辑样本索引成组；
- 明确四条无线链路的信道、地址和时隙策略，避免同信道同时发射碰撞；
- 对四路队列设置背压、水位报警和独立故障隔离；
- 某一路丢失时输出显式缺失区间，不复制旧样本伪造数据；
- 测量四路吞吐、最大服务延迟、队列高水位和逻辑 offset；
- 注入单节点断电、射频中断、ZYNQ 暂停读取和公共 SYNC 重发故障。

完整 V3 验收通常需要 4 块 TX、4 块 RX 和 ZYNQ。板卡不足时可用软件帧注入验证
汇聚算法，但不能据此宣称四路无线系统已经通过。

## 5. V4：第二代空中协议

V4 同时升级 TX 和 RX 的空中帧语义；“第二代空中协议”与工程版本 V4 是两个不同
层级的编号。计划字段至少包括：

- `protocol_version`、`node_id`；
- 32 位 `frame_seq`、`batch_seq`；
- `sample_start_index` 和实际 `sample_count`；
- 高分辨率 `sample_time_ticks`；
- `sync_epoch` 或 START epoch；
- 语义头 CRC、数据 CRC 和明确的端序；
- XYZ 三轴完整分组约束及协议能力协商。

升级前需重新计算固定 204 字节帧中的头部和样本容量，提供版本拒绝或兼容策略，
并用录制帧做主机端编解码、回绕、损坏和跨版本测试。

## 6. V5：物理同步和漂移控制

仅在四块 RX 上接公共 SYNC，只能统一接收端逻辑起点，不能让远端四块 MEMS 在同一
物理时刻采样。V5 需要新增到 TX 的同步控制路径，计划包括：

- 评估广播 START/SYNC、双向时钟同步或外部同步线的可行性；
- TX 侧用硬件定时器标记真实采样时刻；
- 建立 ZYNQ、RX、TX 和 MEMS 采样时钟之间的时间模型；
- 测量固定偏差、随机抖动和长期 ppm 漂移；
- 明确采用重采样、插值、校正索引还是只报告漂移；
- 给出同步精度、重同步时间和失锁检测的量化指标。

在完成这一阶段前，文档只能使用“逻辑对齐”，不能使用“四路物理同步采样完成”。

## 7. V6：成熟工程化

计划内容：

- 8 小时、24 小时及更长期老化测试和自动报告；
- 上电次序、节点热插拔、看门狗、链路失联和自动恢复；
- 队列容量、SPI 时钟、RF 参数和功耗的联合优化；
- 固件版本、板卡身份、配置和日志的可追溯性；
- 固定协议一致性测试、回归测试矩阵和发布清单；
- 定型转接板、EMC/信号完整性、供电和温升验证；
- 根据应用威胁模型决定是否增加认证、加密和密钥管理；
- 冻结可复现构建环境、量产烧录方式和现场升级/回滚策略。

V6 完成后才适合把项目称为可重复部署的“四路同步无线采集—ZYNQ 汇聚”成熟版本。

## 8. 每一版共同的发布规则

每个版本必须同时提供：

1. 固件源码、固定的构建命令和工具链版本；
2. 可审计的协议/引脚/内存文档；
3. 离线构建和主机测试结果；
4. 对应硬件范围的原始测试数据；
5. 已知限制、未完成项和回退方式；
6. 不修改 V1 默认链路时的回归证据。

没有接入对应硬件时，只能标记“离线构建/软件预集成通过”，不能把接口定义或模拟器
结果写成硬件联调通过。
