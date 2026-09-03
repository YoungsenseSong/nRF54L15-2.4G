# nRF工程新对话入口

本文件是切换Codex对话、组合工作区或论文整理时的最短入口。详细历史证据和每次操作
仍以仓库根目录的[`handoff.md`](../handoff.md)为准。

## 1. 当前目标与边界

系统目标：

```text
IIM-42352 -> TX nRF54L15 -> 2.4 GHz ESB -> RX nRF54L15
            -> CH0 SPIS + DRDY + SYNC_IN -> ATK-DF7020 ZYNQ
```

当前只联调CH0，不展开CH1至CH3。ZYNQ是SPI Master，RX0是SPI Slave。无线空中协议、
RF channel 40、4 Mbps、no-ACK、204-byte frame、reorder、64-record queue和248-byte
FPGA record布局均不得因SPI调试而随意改变。

## 2. 仓库与硬件身份

- 仓库：`nrf54l15-connectkit`
- 分支：`codex/rf-link-2p4g-devlog`
- 本次并发修复前远端基线HEAD：`71c49e4a0dda789893b8bfce881d0c8d1db45f6c`
- SDK：NCS 3.1.0，Zephyr 4.1.99
- RX0 probe：`820D9A5F0F3BA22B8E4ES`，通常为COM11
- TX0 probe：`820D9A5F0F3CEA5784A8F`，通常为COM10
- nRF板：nRF54L15 Connect Kit，设计资料采用Rev.A
- FPGA板：正点原子ATK-DF7010/7020P底板V3.9、CF7010B/7020B核心板、ZYNQ-7020
- RX0 `VDD_GPIO`：用户实测约3.3 V；FPGA Bank13 VCCO=3.3 V/LVCMOS33

迁移或切换对话不得reset、checkout、clean；操作前先检查工作树状态。

## 3. 已验证状态

### CH0无线Gate A

- q64 30分钟：RX 224,158 valid frames、21,352,320 valid samples。
- 实际丢失44 frames/4,224 samples，丢帧率0.019625%；duplicate=0、bad=0、
  payload read error=0、radio queue overflow=0。
- 500-frame stream样本中11个完整batch均通过；每batch为
  `42 x 96 + 1 x 64 = 4096` samples。

### CH0 nRF到ZYNQ

- FPGA实板已由用户确认GET_INFO、GET_STATUS、ARM_SYNC、START_STREAM和PEEK可运行。
- 首轮所有真实record在FPGA header CRC16失败；根因是nRF错误使用reflected
  `crc16_ccitt()`。
- nRF record生成和stage校验现已统一为CRC-16/CCITT-FALSE：poly `0x1021`、
  init `0xFFFF`、refin/refout false、xorout `0x0000`，覆盖record bytes0..37。
- 修正版已clean build并烧录RX0；映像SHA256：
  `559E07A5508AFE37C171AE03010826917E890B8C2C33E88701CBF0D24AA152F5`。
- 修正版COM11启动正常；断电后FPGA未重新发起事务，因此FPGA侧CRC PASS仍待复验。
- 后续实板日志出现`submit_errors=49`。源码确认RX主线程和SPIS worker可并发进入
  `fpga_transport_service()`并对同一队首重复submit；现已用Zephyr mutex串行化完整服务
  事务，保留COMMIT后立即stage下一条记录的行为。
- 并发修复的软件验证：Python 14/14 PASS；生产C ztest交叉编译通过但因
  `QEMU-NOTFOUND`未运行；CH0 clean build为Flash 79,708 B、RAM 53,416 B，镜像SHA256
  `82B2FF7D32666E2D828A364FBC6191A612C4E175956634F3C7EC731BB68F5291`。该镜像尚未烧录，
  `submit_errors=0`仍需实板复验。

## 4. CH0接口冻结值

| 项目 | 冻结值 |
| --- | --- |
| SPI | mode0，MSB first，首次1 MHz，nRF上限8 MHz |
| 事务A | 独立CS覆盖完整8-byte request |
| A/B间隔 | 首轮至少1 ms |
| 事务B | 独立CS覆盖完整260-byte response，MOSI填0 |
| CS timing | setup >=1 us，hold >=1 us，inactive >=300 ns |
| DRDY | 高有效保持型；正确COMMIT最后一条后才可拉低 |
| SYNC_IN | 上升沿，首测1 us高脉冲、空闲低、间隔>=1 ms |
| RESET_N | 首轮不接；驱动方式和最小脉宽仍UNKNOWN |
| Header CRC | CRC-16/CCITT-FALSE，bytes0..37 |
| Payload CRC | 原有CRC32 IEEE，bytes40..243，不变 |

CH0 nRF物理引脚：SCK J4-9/P2.01、MOSI J4-12/P2.04、MISO J4-10/P2.02、
CS_N J4-13/P2.05、DRDY J4-31/P1.10、SYNC_IN J4-30/P1.09、GND J4-39。
RESET_N J4-38仅预留。

## 5. 当前构建和测试入口

当前源码位于F:，从D: West根目录经受控junction构建：

```powershell
cd D:\nRF54L15\NCS-Project
.\.venv\Scripts\Activate.ps1
cd .\MultiSensorResearch-nrf
```

CH0 SPIS clean build：

```powershell
west build -p always -d build_workspace_ch0_spis `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=ch0_spis.conf" `
     "-DEXTRA_DTC_OVERLAY_FILE=ch0_spis.overlay"
```

主机测试：

```powershell
python -m unittest discover -s tests -p test_rf_link_future.py -v
```

生产C ztest可clean build到`qemu_cortex_m3`，但当前主机没有QEMU executable，故只能
报告编译/链接成功，不能报告C runtime PASS。

## 6. 下一步唯一主要变量

先烧录SHA-256为`82B2...F5291`的并发修复镜像，再重新Program FPGA bitstream并重复
START_STREAM、PEEK和COMMIT：

1. FPGA重新计算record header bytes0..37，必须CRC PASS；
2. nRF COM11应看到`started>0`、`req_xfer/rsp_xfer/records`增长；
3. payload CRC32仍须PASS；
4. 重复PEEK保持相同队首；正确COMMIT后才推进；
5. 保留FPGA日志、COM11日志和逻辑分析仪证据。
6. 在同一统计窗口确认`submit_errors=0`且records/pop_total持续推进；queue overflow另按
   吞吐与背压问题分析，不能与本次竞态混为一项。

在这一步通过前，不得宣称CH0 SPI record闭环完成。

## 7. 新对话建议首条指令

```text
先读取nrf54l15-connectkit/docs/PROJECT_CONTEXT.md和根目录handoff.md最后两节，
检查git status并保留全部未提交修改。当前只处理CH0。先确认FPGA已重新Program，
然后复验CRC-16/CCITT-FALSE修正版的START_STREAM/PEEK/COMMIT闭环；软件构建、
nRF板上日志和FPGA/逻辑分析仪证据必须分开报告。
```
