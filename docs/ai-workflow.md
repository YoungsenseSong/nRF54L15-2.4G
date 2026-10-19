# AI 协作工作流

这套仓库把 AI 当作工程协作者，而不是只负责生成代码的聊天工具。AI 的工作结果必须同时落在代码、设计文档和验证记录中。

## AI 每次接手工程时的读取顺序

1. 根目录 `AGENTS.md`；
2. 当前工程目录的 `AGENTS.md`；
3. 当前工程 `README.md` 和 `project.yaml`；
4. `docs/design.md`、`docs/spec.md`、`docs/getting-started.md`；
5. `docs/verification.md` 和最近的 `docs/changes/` 记录；
6. `git status` 和与任务相关的代码文件。

## AI 每次修改时的输出闭环

```text
确认任务范围
  ↓
找到对应指标和代码入口
  ↓
小范围修改
  ↓
运行编译、仿真、静态检查或上板验证
  ↓
新增改进记录
  ↓
同步设计/指标/上手/验证文档
  ↓
汇报修改文件、验证结果和未解决问题
```

## 对用户最省事的使用方式

在已经包含本规范的仓库里，日常只需要描述目标，例如：

> 修复 `projects/xxx` 中的 DMA 丢样问题。先检查工程文档和最近改进记录，按仓库规范完成代码、验证和改进记录，不要重排原有文档格式。

如果任务只需要分析，不要让 AI 修改文件：

> 只分析 `projects/xxx` 的时钟和 CDC 风险，不修改代码，最后给出建议改动清单。

如果任务需要新工程：

```bash
python scripts/new_project.py projects/my_project --type fpga --name "My Project"
```

然后先填写 `docs/spec.md`，再写设计和代码。指标先写清楚，后续的改动记录才有可比较的依据。

## 什么时候应该拆分工程

同一个系统的 FPGA、MCU 和上位机代码，如果需要同步联调，可以放在同一个工程目录中；如果工具链、发布周期和负责人明显不同，再拆成多个 GitHub 仓库，并让每个仓库都保留同样的 `AGENTS.md` 和文档结构。
