# 参考资料

本目录保存团队实际使用的原始资料：

- C题_基于无线通信的数字钥匙实验系统.docx：比赛题目原文。
- BU03_V1.1.0_规格书_20240724.pdf：BU03模块厂家规格书。
- BU03-Kit_V1.1.2规格书20260515B.pdf：BU03-Kit开发板规格。
- BU03_BU04_AT_command_cn_V1.0.6.pdf：BU03/BU04角色、配对和输出配置。
- BU03_BU04串口协议_V1.0.1.pdf：厂家串口协议。
- BU03_UART2_测距数据帧协议_20240919.pdf：旧TWR方案专项说明。

当前方案为 BU03 Tag + BU04 PDOA Base。BU04-Kit 官方资料：

- 产品页：https://en.ai-thinker.com/pro_view-159.html
- 规格书：https://en.ai-thinker.com/Uploads/file/20241018/20241018150326_27432.pdf

BU04 Hex 31字节帧的实测偏移、XOR范围和 COM25 样本记录在
docs/BU04_PDOA_PROTOCOL.md，并固化在 tests/test_bu04_pdoa.c。若厂家资料与实测
存在差异，应先保留原始串口抓包，再修改解析器和测试。

ULM3/PDOA商家套件只用于方案调研，不是当前采购实现，不能套用其协议或引脚。

若题目原文、现场说明与仓库描述冲突，以最新官方题目和现场裁判要求为准。
