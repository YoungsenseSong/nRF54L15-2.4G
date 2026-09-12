# nRF54L15 工程交接（R005/r1）

## 工程身份与用途
- 本机目录/Git 根：`F:/OliverS/MultiSensorResearch/nrf54l15-connectkit`；迁移后以仓库根为基准，不依赖该绝对路径。
- HEAD `6172e86c039c61708dea63926ea59babeda08cea`，分支 `codex/rf-link-2p4g-devlog`，上游 `youngsense/codex/rf-link-2p4g-devlog`，本轮前后 clean。
- 发布目标应核对 `youngsense`（`YoungsenseSong/nRF54L15-2.4G`）；`origin` 是 Makerdiary 上游板级仓，`ins1ghtt` 是另一历史远端，禁止误推。
- 用途：IIM-42352→TX FLPR/CPUAPP→私有 2.4 GHz ESB→RX；CH0 集成配置使 RX 作为 ZYNQ SPIS 数据源。入口为 `applications/rf_link_tx/`、`applications/rf_link_rx/` 和 `applications/rf_link_README.md`。

## 当前阶段与证据分层
- **源码可开发：是。** 仓库、板定义、应用、配置和文档齐全；west 工作区通过 junction 绑定到本仓。
- **本轮构建：通过。** CH0 SPIS RX 在隔离输出目录 clean build，退出码 0；`merged.hex` SHA-256 为 `bfc93da008504883d99aabfc8794799305f203b76d62d3ee2ebe1ee95acdfc97`。
- **可测试范围：** 无板可做 west 构建及主机数据/批次工具的离线输入测试；有授权和硬件后才能做 RF、UART、SPIS/DRDY/SYNC。
- **已有板测证据：** 生产文档记录 CRC 修正版 CH0 与 ZYNQ 曾形成 PEEK/COMMIT 功能闭环，`records=1957`、`crc_errors=0`；同时存在 `invalid_cmd`、`short_xfer`、`submit_errors` 和 record queue overflow，不能表述为持续无损或稳定版。
- **r8 隔离候选：BLOCKED/HOLD。** 它不是本仓生产基线；不得集成、烧录或包装成可上板稳定版。阻塞为 stock Zephyr 缺少已接受的身份化 END→join→retire 接口、外部 CSN 释放无法由应用保证、无目标板顺序/恰好一次释放证据。

## 环境依赖
- nRF Connect SDK 3.1.0 系列/Zephyr（仓内文档记录 NCS 3.1.0）；本机 west 1.5.0、NCS Python 3.11.3。
- west topdir 必须是一个完整 NCS workspace；本机为 `D:/nRF54L15/NCS-Project`。`west config manifest.path` 当前为 `MultiSensorResearch-nrf`，该目录是指向本仓的 junction。
- 不复制 `.west`、NCS/Zephyr/nrfx、toolchain、`.venv` 或缓存到仓库。另一台机器应安装匹配 NCS 后，把 manifest.path 指向本仓（或在 NCS topdir 建立明确映射）。
- 板：Makerdiary nRF54L15 Connect Kit Rev.A；烧录工具通常为 pyOCD/CMSIS-DAP，但本轮未连接硬件。

## 从仓库开始开发
1. 克隆发布仓并记录目标 commit；另行安装匹配的 NCS 3.1.0 环境。
2. 从 NCS workspace 激活环境，令 west manifest 可解析本仓；先执行 `west topdir`、`west config manifest.path` 和 `git status --short --branch`。
3. 默认 TX：`west build -p always --sysbuild -d <out>/tx -b nrf54l15_connectkit/nrf54l15/cpuapp applications/rf_link_tx`。
4. 默认 RX：`west build -p always -d <out>/rx -b nrf54l15_connectkit/nrf54l15/cpuapp applications/rf_link_rx`。
5. CH0 RX：在 RX 命令后添加 `-- -DEXTRA_CONF_FILE=ch0_spis.conf -DEXTRA_DTC_OVERLAY_FILE=ch0_spis.overlay`。
6. 输出目录应置于仓外或被忽略的 `build*`；不要复用他人的缓存作为验证。

## 构建与测试入口
- 规范说明：`applications/rf_link_README.md`。
- 本工作区辅助脚本：`tools/build_nrf_ch0.ps1`，但其本机 NCS 根和 junction 路径硬编码，发布前应改为参数或只作为工作区工具，不宜进入 nRF 仓。
- 主机工具：`dump_rx_frames.py`、`verify_mems_batches.py`、`save_serial_csv.py`。其中默认 COM/输出路径是本机值，调用时应显式传参。
- 本轮实际命令、日志、退出码和哈希见 [验证摘要](HANDOFF_VALIDATION.md)。

## 烧录前条件
- 用户/Astra 明确批准具体 commit、镜像哈希、板号/探针号、供电和接线；确认所用是生产基线而非 r8 BLOCKED 候选。
- CH0 映射、mode 0、MSB first、1 MHz、DRDY/SYNC 和 RESET_N 断开条件按冻结文档逐项复核；nRF 和 ZYNQ 成对版本必须匹配。
- 保留旧镜像哈希和可恢复探针；先保存启动 UART，再做 PEEK/COMMIT，遇到 UNKNOWN、CSN 不释放、CRC/身份不一致即停止。

## 回退
- 源码：切回已记录的旧 commit/分支；不要 reset/clean 含未保存证据的工作树。
- 固件：用已验证旧 `merged.hex` 成对回退，不混用 r8 与生产 ZYNQ。
- NCS 环境：保留 NCS/toolchain 版本记录；新环境失败时回到原 workspace，而不是复制虚拟环境。

## 已知问题与首个建议任务
- `dump_rx_frames.py`、旧指南和若干 handoff 命令含 `D:/...`、COM7/COM10/COM11 等本机默认值；需要参数化/示例化后再称可移植。
- 仓内有约 39.7 MB STEP 模型和多个 Makerdiary 文档资产；发布前确认上游许可和是否确需随开发仓分发。
- 当前 CH0 队列 overflow 与恢复错误未消除；首次建议任务是基于生产基线建立版本配对/有界无板回归，再由 Astra裁决后安排板测；不要从 r8 BLOCKED 候选开始。


## 本次发布说明

本文件基于已冻结的 R005/r1 Opus 交付，由 Astra 核对清单和当前 Git 基线后整理。上述 HEAD 是被验证的源码基线，不是加入交接文档后的提交号。构建与仿真由 Opus 执行；Astra 本次不宣称独立重跑全部构建。仅发布现有开发分支和说明，不新增稳定版或板测通过声明。
