# 改进记录：发布当前 RX/TX 与同步协调器工程

- 日期：2026-10-19
- 类型：工程重构
- 关联指标：`docs/spec.md` 中的当前无线链路和同步原型入口
- 关联提交：待提交

## 1. 改动前状态

生产 RX/TX 位于仓库既有 `applications/`；同步协调器只存在于工作区诊断目录，缺少可直接从 GitHub 复建的应用入口。

## 2. 改动来源与思路

按 `ic-engineering-workspace` 模板补齐工程入口，保留生产代码路径，把已构建的同步原型抽取为独立应用。

## 3. 具体改进方法

- 新增 `applications/sync_minimal/` 源码、配置、overlay、构建脚本和 UART 工具。
- 新增 `projects/rf_link_current/` 设计、规格、上手和验证文档。

## 4. 涉及文件

- `applications/sync_minimal/`
- `projects/rf_link_current/`
- `AGENTS.md`
- `docs/00-repository-standard.md`
- `docs/ai-workflow.md`

## 5. 验证方法

- 使用原同步原型构建记录核对入口和配置。
- 未在本次整理中重新烧录或进行新的板测。

## 6. 改动后效果

| 指标 | 改动前 | 改动后 | 结论 |
|---|---|---|---|
| 同步协调器可复建入口 | 工作区诊断目录 | 仓库 `applications/sync_minimal` | 已整理 |
| 验证边界 | 分散在诊断记录 | 项目文档集中说明 | 已整理 |

## 7. 限制与下一步

- TX1 双响应端和 P1.09 同仪器边沿差仍待实测。
