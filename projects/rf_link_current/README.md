# nRF54L15 当前无线采集链路

当前 RX、TX 和同步协调器工程的整理入口。生产 RX/TX 源码仍位于仓库既有的 `applications/rf_link_rx/` 与 `applications/rf_link_tx/`；同步协调器可复建工程位于 `applications/sync_minimal/`。

- 类型：嵌入式无线采集
- 状态：开发中
- 负责人：Oliver
- 当前结论：RX/TX 生产链路保留；RX1→RX2 同步协调器协议原型已观测通过，TX1 双响应端和物理边沿差仍待验证。

## 工程入口

- 设计思路：[docs/design.md](docs/design.md)
- 工程指标：[docs/spec.md](docs/spec.md)
- 上手指南：[docs/getting-started.md](docs/getting-started.md)
- 验证记录：[docs/verification.md](docs/verification.md)
- 改进记录：[docs/changes/](docs/changes/)
- 生产 RX：[../../applications/rf_link_rx/](../../applications/rf_link_rx/)
- 生产 TX：[../../applications/rf_link_tx/](../../applications/rf_link_tx/)
- 同步协调器：[../../applications/sync_minimal/](../../applications/sync_minimal/)

## 版本里程碑

| 版本 | 日期 | 内容 | 状态 |
|---|---|---|---|
| v0.1 | 2026-10-19 | 当前 RX/TX 与同步协调器整理 | 开发中 |
