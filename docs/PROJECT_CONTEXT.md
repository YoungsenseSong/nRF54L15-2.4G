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
- 本文更新时HEAD：`4e360863a3569c9494197ec701604404519d811d`
- SDK：NCS 3.1.0，Zephyr 4.1.99
- RX0 probe：`820D9A5F0F3BA22B8E4ES`，通常为COM11
- TX0 probe：`820D9A5F0F3CEA5784A8F`，通常为COM10
- nRF板：nRF54L15 Connect Kit，设计资料采用Rev.A
- FPGA板：正点原子ATK-DF7010/7020P底板V3.9、CF7010B/7020B核心板、ZYNQ-7020
- RX0 `VDD_GPIO`：用户实测约3.3 V；FPGA Bank13 VCCO=3.3 V/LVCMOS33

工作树包含未提交修改和未跟踪的新文件。迁移或切换对话不得reset、checkout、clean，
也不能只依赖HEAD重建当前状态。

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

从NCS West根目录的虚拟环境进入仓库：

```powershell
cd D:\nRF54L15\NCS-Project
.\.venv\Scripts\Activate.ps1
cd .\nrf54l15-connectkit
```

CH0 SPIS clean build：

```powershell
west build -p always -d build_codex_ch0_spis_crcfix `
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

重新Program FPGA bitstream后，重复START_STREAM和PEEK：

1. FPGA重新计算record header bytes0..37，必须CRC PASS；
2. nRF COM11应看到`started>0`、`req_xfer/rsp_xfer/records`增长；
3. payload CRC32仍须PASS；
4. 重复PEEK保持相同队首；正确COMMIT后才推进；
5. 保留FPGA日志、COM11日志和逻辑分析仪证据。

在这一步通过前，不得宣称CH0 SPI record闭环完成。

## 7. 新对话建议首条指令

```text
先读取nrf54l15-connectkit/docs/PROJECT_CONTEXT.md和根目录handoff.md最后两节，
检查git status并保留全部未提交修改。当前只处理CH0。先确认FPGA已重新Program，
然后复验CRC-16/CCITT-FALSE修正版的START_STREAM/PEEK/COMMIT闭环；软件构建、
nRF板上日志和FPGA/逻辑分析仪证据必须分开报告。
```
