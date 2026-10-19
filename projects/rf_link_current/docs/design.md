# 设计思路

## 1. 问题背景

采集端使用 IIM-42352 和 nRF54L15 采样、打包并通过私有 ESB 无线链路发送；接收端校验帧、维护序号和可选 SPI/FMC 传输。多节点时间一致性需要区分无线到达时间、响应端硬件事件时间和传感器实际采样时间。

## 2. 总体架构

```text
IIM-42352 -> TX FLPR/CPUAPP -> ESB -> RX CPUAPP -> SPI/DRDY/SYNC -> FPGA
                                      ^
                                      |
                         sync_minimal coordinator/responder
```

同步协调器使用 ESB 广播轮次；响应端在 RADIO ADDRESS 事件后通过 DPPI 启动 TIMER，并在固定延时后产生 GPIO 观察脉冲。

## 3. 模块划分

| 模块 | 职责 | 输入 | 输出 | 时钟/复位 |
|---|---|---|---|---|
| `applications/rf_link_tx` | MEMS 采集、批次共享内存、无线发送 | IIM-42352 SPI/DRDY | ESB 帧、状态 UART | FLPR/CPUAPP |
| `applications/rf_link_rx` | ESB 接收、校验、重排、SPI 传输 | ESB 帧、FPGA 命令 | 记录、状态和 SPI | CPUAPP |
| `applications/sync_minimal` | 同步广播和响应端硬件定时 | ESB | 轮次状态、P1.09 观察脉冲 | RADIO/DPPI/TIMER20 |

## 4. 关键设计决策

- 生产 RX/TX 的 204 字节无线帧格式保持不变。
- 同步原型单独使用 channel 40、pipe 0、prefix `0x54`，不修改生产协议。
- 主机 UART 到达时间只作状态证据，不能代替板上边沿测量。
- 同步原型不直接宣称 IIM ADC 已经同刻。

## 5. 时序、CDC 和复位

同步响应端由 RADIO ADDRESS 事件通过 DPPI 同时清零/启动 TIMER20；比较事件驱动 GPIOTE 输出。当前实现的 2000 us 延时和 P1.09 测试点必须结合实际 HFXO、GPIO 连接和示波器测量复核。

## 6. 资源和功耗考虑

同步原型构建记录约 62–64 KiB Flash、12.4 KiB RAM；该数值来自独立构建记录，不等于生产 RX/TX 运行时资源。

## 7. 风险与未决问题

- [ ] TX1 作为第二响应端的同步镜像启动和 UART 观测
- [ ] RX2/TX1 P1.09 同仪器边沿差和漂移
- [ ] HFXO 实际来源、频率误差和温漂
- [ ] 同步事件与 IIM DRDY/FSYNC 的实际关联
