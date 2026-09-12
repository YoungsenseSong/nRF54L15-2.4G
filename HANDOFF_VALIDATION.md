# R005/r1 验证摘要

- 被验证源码：`6172e86c039c61708dea63926ea59babeda08cea`。
- Opus 执行：NCS 3.1.0 系列环境，west 1.5.0，CH0 SPIS RX clean build 退出0。
- 配置：`applications/rf_link_rx`，board `nrf54l15_connectkit/nrf54l15/cpuapp`，`ch0_spis.conf` 与 `ch0_spis.overlay`。
- `merged.hex`：224229 bytes，SHA-256 `bfc93da008504883d99aabfc8794799305f203b76d62d3ee2ebe1ee95acdfc97`。
- 未验证：本轮 TX、默认 RX、烧录、串口、RF/板测、r8恢复候选。

Astra核验交付清单和源码基线，不冒称重新执行west构建。完整原始日志保留在本地工作区 `artifacts/collaboration/R005/r1/opus/logs/nrf_build.*.log`，不是本仓附件；需要复现时按HANDOFF重新生成。此源码交接不附带获准烧录镜像。
