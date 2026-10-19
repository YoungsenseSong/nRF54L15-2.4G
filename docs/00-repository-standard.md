# 仓库规范

## 1. 工程类型

当前模板覆盖三类工程：

| 类型 | 主要内容 | 常见目录 |
|---|---|---|
| FPGA/数字 IC | RTL、testbench、约束、IP、上板验证 | `rtl/`、`tb/`、`constraints/`、`results/` |
| MCU/嵌入式 | BSP、驱动、应用、下载和调试 | `firmware/`、`hardware/`、`scripts/` |
| 混合系统 | FPGA 与 MCU、模拟前端或传感器联调 | 上述目录组合，并在 `design.md` 中画清数据流 |

## 2. 文档更新触发条件

| 变化 | 必须更新 |
|---|---|
| 接口、引脚、时钟、位宽、采样率变化 | `docs/spec.md`、`docs/changes/` |
| 启动、编译、下载步骤变化 | `docs/getting-started.md`、`docs/changes/` |
| 架构或模块职责变化 | `docs/design.md`、`docs/changes/` |
| 仿真、上板或测量数据变化 | `docs/verification.md`、`docs/changes/` |
| 仅修正错别字 | 可直接修改；若改变技术含义，仍需记录 |

## 3. 版本记录

改动记录使用日期加序号，不使用“最终版”“最新版”等容易失效的名称。重要里程碑可以在工程 `README.md` 中增加版本表：

| 版本 | 日期 | 主要变化 | 验证状态 |
|---|---|---|---|
| v0.1 | YYYY-MM-DD | 初始工程 | 待验证 |
