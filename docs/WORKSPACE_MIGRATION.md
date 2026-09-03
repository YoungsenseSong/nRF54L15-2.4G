# 组合工作区与nRF工程迁移清单

## 2026-09-03迁移完成后的实际入口

源码现位于`F:\OliverS\MultiSensorResearch\nrf54l15-connectkit`。为避免West 1.5.0在
D: workspace与F:源码之间计算相对路径时报跨盘错误，已建立目录junction：

```text
D:\nRF54L15\NCS-Project\MultiSensorResearch-nrf
  -> F:\OliverS\MultiSensorResearch\nrf54l15-connectkit
```

West workspace的`manifest.path`已从`nrf54l15-connectkit`切换为
`MultiSensorResearch-nrf`。后续构建必须使用junction路径，不再把D:中的旧源码副本作为
manifest。CH0 SPIS已从该入口执行`-p always` clean build成功：Flash 79,672 B、RAM
53,400 B；`merged.hex` SHA-256为
`559E07A5508AFE37C171AE03010826917E890B8C2C33E88701CBF0D24AA152F5`。

组合工作区根目录提供`tools/build_nrf_ch0.ps1`封装上述检查和命令。以下章节保留迁移前的
方案与决策依据；其“尚未验证新路径构建”的状态由本节覆盖。

## 推荐方案：VS Code多根工作区，不搬动源码

nRF仓库位于一个完整West workspace内：

```text
D:\nRF54L15\NCS-Project\
  .west\
  .venv\
  nrf\
  zephyr\
  modules\
  ...
  nrf54l15-connectkit\
```

最简单、稳定、占用空间最少的方式，是在新的系统工作目录只建立一个
`.code-workspace`文件，用绝对路径引用现有三个工程，并新建独立`paper/`目录。nRF源码
无需移动，也不会破坏`west topdir`、NCS模块解析、Python虚拟环境和已有构建脚本。

建议新工作区布局：

```text
Integrated-System-Workspace\
  integrated-system.code-workspace
  paper\
    README.md
    outline\
    figures\
    tables\
    references\
    evidence\
```

`integrated-system.code-workspace`示例：

```json
{
  "folders": [
    {"name": "nRF54L15", "path": "D:/nRF54L15/NCS-Project/nrf54l15-connectkit"},
    {"name": "FPGA-F730", "path": "F:/OliverS/AI_Embedded/F730+FPGA"},
    {"name": "Third-Project", "path": "<第三个工程的绝对路径>"},
    {"name": "Paper", "path": "./paper"}
  ],
  "settings": {
    "files.encoding": "utf8",
    "terminal.integrated.defaultProfile.windows": "PowerShell"
  }
}
```

该文件应放在新组合工作区，不应放进任一子工程仓库，除非三个工程共同使用同一版本库。

## nRF侧无需移植的内容

采用推荐的多根工作区方案时，nRF侧**没有任何目录需要复制或移动**。只把现有仓库路径
加入`.code-workspace`即可。

以下内容尤其不应复制到组合工作区：

- `build/`、`build_*`、`twister-out*`：体积大、含绝对路径、可重新生成；
- `.venv/`：仍使用`D:\nRF54L15\NCS-Project\.venv`，虚拟环境不可稳定搬运；
- `__pycache__/`、`.pytest_cache/`、`site/`：缓存或生成物；
- NCS的`nrf/`、`zephyr/`、`modules/`等依赖副本：不要为组合工作区重复复制；
- 临时串口CSV和一次性build log：论文需要的原始证据应挑选后复制到`paper/evidence/`，
  并记录来源、日期和SHA256，而不是整目录搬运。

## 如果必须物理迁移nRF源码

不要挑选若干源文件拼装新工程。最小可靠单位是**整个
`nrf54l15-connectkit/` Git仓库**，包括：

- `.git/`：分支、提交和历史；
- `.gitignore`、`west.yml`、根`CMakeLists.txt`、`Kconfig`、`VERSION`；
- `applications/`、`boards/`、`samples/`、`scripts/`、`tests/`；
- `docs/`和根`handoff.md`；
- 根目录Python采集/验证脚本；
- 当前所有未跟踪但属于工程的新文件，例如CH0 conf/overlay、SPIS backend和C ztest。

排除上一节列出的build/cache目录。当前工作树是dirty，普通`git clone`只会得到HEAD，
会遗漏本次CH0实现和CRC修正。物理迁移前应优先审计并提交当前变更；若暂不提交，则必须
复制整个工作树并用迁移前后的文件清单和SHA256核对，不能只复制Git已跟踪文件。

物理迁移后仍建议让源码留在原West workspace外部引用，而不是搬运整个NCS。构建时先
进入原NCS环境，并显式设置Zephyr/NCS环境；如果`west topdir`无法从新路径找到
`D:\nRF54L15\NCS-Project`，应使用Nordic的VS Code扩展选择原NCS 3.1.0工具链，或把新路径
作为符号链接/junction挂到West workspace内。不要修改源码中的板名或overlay来“修复”
工具链路径问题。

## 已选择物理迁移时的最小稳定精简方案

用户已选择把nRF工程物理复制到新的组合工作区。当前仓库总文件约1108.45 MiB，其中根
目录40个`build`/`build_*`目录约1010.02 MiB；完整复制后只移除这些可再生构建目录，
副本约剩98.43 MiB。这个方案比手工挑选`applications/`子目录稳定，也能保留板级定义、
文档证据、Git历史和所有dirty文件。

推荐顺序：

1. 关闭正在占用构建目录的终端和调试会话；
2. 完整复制`nrf54l15-connectkit/`，必须显示隐藏项目并包含`.git/`；
3. 在新副本先运行`git status --short --branch`并与原目录逐行核对；
4. 如需保留当前可直接烧录的CRC修正版，先把
   `build_codex_ch0_spis_crcfix/merged.hex`复制到论文`evidence/nrf/`，同时保存handoff中的
   SHA256；
5. 只在**新副本**预览和删除根目录的`build`/`build_*`；
6. 删除后再次检查Git状态，并从新源码执行一次clean build。

PowerShell安全清理模板如下。必须先把路径替换成新副本的准确绝对路径；第一次只执行
预览部分：

```powershell
$NrfCopyRoot = (Resolve-Path -LiteralPath 'F:\<新组合工作区>\nrf54l15-connectkit').Path
if ((Split-Path -Leaf $NrfCopyRoot) -ne 'nrf54l15-connectkit') {
    throw "Refusing unexpected target: $NrfCopyRoot"
}

$BuildTargets = Get-ChildItem -LiteralPath $NrfCopyRoot -Directory -Force |
    Where-Object { $_.Name -eq 'build' -or $_.Name -like 'build_*' }

# Dry run: inspect every resolved target before deletion.
$BuildTargets | Select-Object FullName
```

确认输出全部位于新副本且仅为构建目录后，才执行：

```powershell
$BuildTargets | Remove-Item -Recurse -Force
```

不要用`git clean -fdX`做这次精简。它还会删除其他被忽略内容，例如
`applications/.vscode/`、`applications/Dual-Core Project Example/`、临时配置和可能需要
归档的串口数据，范围比“删除构建目录”大。

源码搬出原West workspace后，NCS SDK和虚拟环境仍留在
`D:\nRF54L15\NCS-Project`。新副本不能假定自身可以找到`.west`；构建时从原NCS环境启动，
并为仓库内自定义Connect Kit板显式传入新副本作为`BOARD_ROOT`。例如：

```powershell
cd D:\nRF54L15\NCS-Project
.\.venv\Scripts\Activate.ps1

$NrfSource = 'F:\<新组合工作区>\nrf54l15-connectkit'
west build -p always `
  -d "$NrfSource\build_ch0_spis" `
  -b nrf54l15_connectkit/nrf54l15/cpuapp `
  "$NrfSource\applications\rf_link_rx" -- `
  "-DBOARD_ROOT=$NrfSource" `
  "-DEXTRA_CONF_FILE=ch0_spis.conf" `
  "-DEXTRA_DTC_OVERLAY_FILE=ch0_spis.overlay"
```

这个命令需要在迁移后实际clean build确认；确认前不能把“文件复制完成”写成“新路径构建
成功”。不要复制或移动`D:\nRF54L15\NCS-Project\.venv`、`.west`、`nrf`、`zephyr`和
`modules`到组合工作区。

## 迁移完成检查

1. 新对话能打开`docs/PROJECT_CONTEXT.md`和`handoff.md`；
2. `git status --short`与迁移前一致；
3. `git branch --show-current`和`git rev-parse HEAD`一致；
4. `west topdir`仍指向NCS 3.1.0 workspace；
5. CH0 SPIS clean build命令成功；
6. 不把旧`build_*`中的HEX当成新路径源码生成的证据；
7. 论文引用的日志、截图、CSV附日期、硬件身份、配置和hash。
