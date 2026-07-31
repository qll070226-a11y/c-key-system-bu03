# 硬件与接线

## 系统拓扑

```text
BU03 Tag 0  <---- UWB ---->  Anchor 0 ---- UART2_TX ----> ESP32-S3 GPIO18
                  |              +------ 主USB D-/D+ ---> ESP32-S3 GPIO19/20
                  |          Anchor 1（只供电参与测距）
                  |
                  +------ 门锁端双基站定位
```

Anchor 0和Anchor 1都位于门锁圆柱体内。只有Anchor 0连接ESP32：UART2输出多Anchor距离，主USB完整TWR帧输出真实Tag ID。最终验收模式下门锁必须同时收到近期距离帧和近期真实ID才允许认证。当前未接主USB身份线的调试阶段允许临时开启拨码ID回退，但该模式不能作为身份功能验收结果。

当前几何参数按两块BU03天线中心左右对称布置：Anchor 0为`(-220, 0)mm`，Anchor 1为`(+220, 0)mm`，天线中心间距440mm。坐标原点是门锁圆柱中心，正前方为+Y。若实际装配后不是严格对称，必须分别实测两个天线中心坐标，不能只填写总间距。

当前固件不进行Anchor与Tag高度差补偿。校正和滤波后的UWB测距值直接进入二维双基站定位，因此上位机/TFT显示距离与BU03输出保持同一口径。

## GPIO表

| 功能 | ESP32-S3 GPIO | 连接说明 |
|---|---:|---|
| BU03 UART RX | 18 | Anchor 0 PA2/UART2_TX，ESP端只收 |
| BU03 USB D- | 19 | ESP32-S3原生USB D-，连接Anchor 0主USB D- |
| BU03 USB D+ | 20 | ESP32-S3原生USB D+，连接Anchor 0主USB D+ |
| 拨码bit0 | 4 | 10k上拉到3.3V，开关闭合接地 |
| 拨码bit1 | 5 | 同上，权值2 |
| 拨码bit2 | 6 | 同上，权值4 |
| 拨码bit3 | 7 | 同上，权值8 |
| 蜂鸣器 | 8 | 低电平触发，模块输入端接GPIO8 |
| TFT D/C | 9 | 3.3V逻辑 |
| TFT CS | 10 | 3.3V逻辑 |
| TFT SDI/MOSI | 11 | 3.3V逻辑 |
| TFT CLK | 12 | 3.3V逻辑 |
| TFT SDO/MISO | 13 | 3.3V逻辑 |
| TFT BLK | 14 | 高电平开背光，3.3V逻辑 |
| 闭锁红灯 | 15 | GPIO->330欧姆->LED正极，负极接地 |
| 开锁绿灯 | 16 | 同上 |
| 迎宾灯 | 17 | 同上 |

## TFT

实物为LVSN-TFT-2.4，当前ILI9341初始化可正常显示。接线：

```text
TFT VCC -> 3.3V
TFT GND -> GND
TFT SDI -> GPIO11
TFT CLK -> GPIO12
TFT CS  -> GPIO10
TFT SDO -> GPIO13
TFT D/C -> GPIO9
TFT BLK -> GPIO14
```

商家明确说明VCC只能使用3.3V。曾在VCC约1.7V时完全不亮，改为3.3V后显示正常。最终安装时屏幕横放，旋转到文字从左到右正常阅读。

当前固件使用320x240中文仪表盘，显示钥匙ID、门锁ID、身份认证、径向距离、方位角、当前区域、门锁状态、UWB通信和迎宾灯。中文字库为界面专用16x16点阵，只包含实际使用字符；修改界面中文后需运行`tools/generate_tft_zh_font.py`重新生成字库并执行覆盖检查。

## 拨码和LED

每个拨码位：

```text
3.3V -- 10k --+-- GPIO
               +-- 拨码开关 -- GND
```

开关OFF为0，ON为1。四位全OFF表示ID 0。GPIO4为最低位。

每个LED：

```text
GPIO -- 330欧姆 -- LED正极
LED负极 -- GND
```

## 蜂鸣器

当前蜂鸣器模块为低电平触发：GPIO8低电平时响，高电平时关闭。进入迎宾区时产生500ms低电平脉冲；初始化、故障安全和脉冲结束均保持高电平。使用带驱动电路的蜂鸣器模块，裸蜂鸣器不得直接由GPIO供电。

## 正式电源

当前正式供电尚未实测，计划如下：

```text
12V电池
  +-- DCDC 1: 5V   -> ESP32 5V/VIN、扩展板
  |          3.3V -> TFT VCC
  +-- DCDC 2: 5V   -> Anchor 0主USB VBUS、Anchor 1

所有输出GND在低阻公共点汇接
```

Tag侧：

```text
带保护锂电池 -> 带开关5V升压DCDC -> Tag BU03-Kit 5V
```

两路DCDC必须空载调整并测量合格后再接模块。5V允许4.9到5.1V，3.3V允许3.25到3.35V。两路DCDC的5V输出严禁并联，只允许GND共地。Anchor 0改为由DCDC 2经主USB VBUS单点供电，不再同时从5V插针供电。ESP32调试使用COM口，原生USB口及GPIO19/20留给Anchor 0；调试时避免电脑USB和外部5V反向灌电。UWB电源必须使用焊接、锁紧端子或可靠USB线，不使用漆包线缠绕供电。

Anchor 0主USB数据线建议使用USB转接板或剪开的合格数据线，连接如下：

```text
ESP32 GPIO19 (USB D-) -> Anchor 0 主USB D-
ESP32 GPIO20 (USB D+) -> Anchor 0 主USB D+
公共GND                -> Anchor 0 主USB GND
DCDC 2 5V              -> Anchor 0 主USB VBUS
```

D+和D-成对绞合并尽量短，建议不超过20cm，禁止接反。这里连接的是Anchor 0的“定位测距数据交互口/主USB”，不是CH340 AT指令口。

## 射频布局

- 两Anchor天线中心同高、同一朝向并保持左右对称。
- Tag测试姿态固定，天线不要被手掌、身体、电池或金属遮挡。
- DCDC、电池、蜂鸣器、TFT排线和大电流回路远离UWB天线。
- 最终测量的是天线相位中心坐标，不是PCB外壳边缘。
