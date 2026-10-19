# 改进记录：补齐工程目录导航

- 日期：2026-10-19
- 类型：工程重构

RX、TX 和同步协调器源码继续保留在仓库现有 `applications/` 入口，项目根新增 `firmware/`、`hardware/`、`scripts/`、`results/` 导航，避免复制 Zephyr 工程造成版本漂移。

验证：仓库结构检查通过；RX/TX 和同步协调器的既有构建路径不变。
