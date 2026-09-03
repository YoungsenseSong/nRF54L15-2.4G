# nRF工程论文证据索引

本文件只整理可用于后续论文写作的nRF侧事实与证据边界，不把设计目标、软件构建和硬件
实测混写。引用任何数字前应回看根目录`handoff.md`及原始文件。

## 可直接陈述并注明条件的结果

| 主题 | 结果 | 证据位置 |
| --- | --- | --- |
| 无线参数 | ESB PRX/PTX、4 Mbps、channel 40、no-ACK、204-byte frame | `handoff.md`第2、4节；启动日志 |
| CH0 30分钟 | 224,158 valid frames，21,352,320 samples | `handoff.md`第4节 |
| 实际无线丢失 | 44 frames/4,224 samples，0.019625% | `handoff.md`第4节及原始CSV |
| 批次结构 | 42帧x96样本 + 1帧x64样本 = 4096样本 | `handoff.md`第4节；stream CSV |
| 队列改进 | q16短测出现7次本地overflow；只把depth改为64后30分钟overflow=0 | `handoff.md`第4节 |
| SPI控制面 | FPGA实板已确认GET_INFO/GET_STATUS/ARM_SYNC/START_STREAM/PEEK可运行 | 用户实板报告；`handoff.md`第13节 |
| CRC根因 | Zephyr reflected API与冻结的CCITT-FALSE不一致 | 源码diff；`handoff.md`第13节 |
| CRC修正版 | clean build成功并已烧录RX0 | `handoff.md`第13、14节；HEX SHA256 |

## 只能称为软件验证的结果

- Python contract/reference-model tests：14/14 PASS。
- 生产C ztest已编译/链接，但因QEMU缺失尚无runtime PASS。
- CH0 SPIS CRC修正版clean build：Flash 79,672 B、RAM 53,400 B。
- GPIOTE20-GPPI/DPPI-TIMER20硬件capture已实现并构建；没有完整SYNC逻辑分析仪验收。

## 尚不能写成已完成的结论

- CRC修正后的FPGA header CRC PASS；FPGA断电后尚未重新Program并复验。
- 正确COMMIT、错误/重复COMMIT的完整实板闭环。
- 10,000条连续record零SPI/parser错误。
- CH0 SPI连续2小时长测。
- SYNC_IN最小可靠脉宽及最终时间精度。
- RESET_N驱动方式和最小脉宽。
- 四通道同步或四路并发；当前仅CH0。
- 无线“零丢包”；实测明确存在44帧丢失。

## 建议论文证据目录元数据

每个放入新工作区`paper/evidence/`的文件，至少配一个同名Markdown或CSV索引，记录：

- 日期、持续时间和测试目的；
- TX/RX/FPGA板卡型号、probe ID和角色；
- Git commit、dirty状态、固件/bitstream SHA256；
- NCS/Vivado版本；
- RF、SPI、DRDY、SYNC配置；
- 接线版本和逻辑分析仪通道；
- 原始文件路径；
- 软件结果、nRF板上结果、FPGA板上结果分别判定；
- 已知异常及是否排除预热、复位、断电等影响。

优先复制原始CSV、纯文本串口日志、ILA/逻辑分析仪导出和必要截图；图表应由脚本从原始数据
生成，避免只有图片而无法复算。
