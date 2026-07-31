# BU04 PDOA Hex 数据协议

本文记录当前 BU04-Kit 在 USER_CMD=1（Hex输出）时的实测协议。依据厂家文档并用
COM25 实际帧验证，固件实现位于 components/c_key_core/bu04_pdoa.c。

## 帧格式

固定31字节，小端：

| 偏移 | 长度 | 字段 | 类型/单位 |
|---:|---:|---|---|
| 0 | 1 | 帧头 | 固定0x2A |
| 1 | 1 | 数据长度 | 固定0x1B |
| 2 | 1 | 序号 | uint8，循环递增 |
| 3 | 2 | 标签短地址 | uint16 |
| 5 | 4 | PDOA角度 | int32，deg |
| 9 | 4 | 距离 | uint32，cm |
| 13 | 2 | UserCmd | uint16 |
| 15 | 4 | First Path Power | int32 |
| 19 | 4 | RX Level | int32 |
| 23 | 2 | Acc X | int16 |
| 25 | 2 | Acc Y | int16 |
| 27 | 2 | Acc Z | int16 |
| 29 | 1 | 校验 | XOR |
| 30 | 1 | 帧尾 | 固定0x23 |

校验为偏移2到28（含）的逐字节异或，不包含帧头、长度、校验字节和帧尾。

## 实测帧

~~~text
2a 1b 0c 19 6e eb ff ff ff 39 00 00 00 00 c0
00 00 00 00 00 00 00 00 00 00 00 00 00 00 96 23
~~~

解析结果：

~~~text
sequence = 0x0C
tag_address = 0x6E19
angle = -21 deg
distance = 57 cm
user_command = 0xC000
checksum = 0x96
~~~

该原始帧已固化为 tests/test_bu04_pdoa.c 回归样本。

## 流式接收

解析器按 0x2A 0x1B 同步，满31字节后同时校验帧尾和 XOR。坏帧会增加
rejected_frames，并在缓存内寻找下一组帧头以恢复同步。主程序只接受距离
0.05到20m、角度-180到180度的帧。

## 身份映射

标签短地址本身来自 UWB 无线帧。当前固件仅将 0x6E19 映射为逻辑钥匙ID0；
未知地址返回无效ID并保持闭锁。门锁拨码设置认可逻辑ID，因此拨码改为非0时
当前钥匙立即认证失败。

## 接口

- BU04 主USB数据口当前为 COM25。
- BU04 排针4为 UART2_TX，正式接 ESP32 GPIO18。
- 两个接口均是二进制数据源，上位机不能直接把它们当 C_KEY_DIAG_V2 文本读取。
- C_KEY_DIAG_V2 由 ESP32 解析后从其 CH343 COM 口输出。
