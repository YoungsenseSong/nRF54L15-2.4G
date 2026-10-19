# 工程上手指南

## 1. 环境

- Nordic Connect SDK 和 Zephyr
- `west`
- nRF54L15 board definition
- Python/pyserial 用于状态采集

## 2. 硬件准备

生产 RX/TX 按仓库既有接线和探针 UID 使用。同步原型接线前确认 RX2/TX1 的 P1.09 没有与 FPGA 或其他输出冲突。

## 3. 获取和配置

```powershell
git clone https://github.com/YoungsenseSong/nRF54L15-2.4G.git
cd nrf54l15-connectkit
```

先读仓库 `README.md`、`TEAM_HANDOFF.md` 和 `applications/rf_link_README.md`。

## 4. 编译和下载

生产 RX/TX 使用仓库已有构建脚本或 `west build` 入口。同步协调器：

```powershell
cd applications/sync_minimal
powershell -ExecutionPolicy Bypass -File .\scripts\build_sync.ps1 -Role coordinator -Board connectkit -Pristine
powershell -ExecutionPolicy Bypass -File .\scripts\build_sync.ps1 -Role responder -Board connectkit -Pristine
powershell -ExecutionPolicy Bypass -File .\scripts\build_sync.ps1 -Role responder -Board tx1 -Pristine
```

烧录前必须保存目标 FICR 和原镜像，结束后恢复生产镜像。

## 5. 运行和观察

```powershell
python .\applications\sync_minimal\scripts\capture_sync_uart.py `
  --coordinator COM11 --responders COM10 COM4 --seconds 30
```

主机日志只能确认协议轮次；边沿差使用同一台示波器或逻辑分析仪。

## 6. 常见问题

| 现象 | 可能原因 | 排查方法 |
|---|---|---|
| 没有同步状态行 | 端口错误或镜像未启动 | 核对 UART overlay、探针和复位 |
| RX 收不到帧 | channel/prefix 或 HFCLK 未启动 | 对照 `sync_minimal` 配置和无线日志 |
| P1.09 无边沿 | 未配置测试引脚或被外部驱动 | 断开冲突线后再测 |
