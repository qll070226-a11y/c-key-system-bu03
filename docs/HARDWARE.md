# 硬件与接线

## 系统拓扑

```text
BU03 Tag 0  <---- UWB ---->  Anchor 0 ---- UART2_TX ----> ESP32-S3
                  |          Anchor 1（只供电参与测距）
                  |
                  +------ 门锁端双基站定位
```

Anchor 0和Anchor 1都位于门锁圆柱体内。只有Anchor 0向ESP32输出包含多Anchor距离的聚合帧。

## GPIO表

| 功能 | ESP32-S3 GPIO | 连接说明 |
|---|---:|---|
| BU03 UART RX | 18 | Anchor 0 PA2/UART2_TX，ESP端只收 |
| 拨码bit0 | 4 | 10k上拉到3.3V，开关闭合接地 |
| 拨码bit1 | 5 | 同上，权值2 |
| 拨码bit2 | 6 | 同上，权值4 |
| 拨码bit3 | 7 | 同上，权值8 |
| 蜂鸣器 | 8 | 高有效，必须经过扩展板驱动管 |
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

软件配置GPIO8高有效，进入迎宾区时产生500ms脉冲。若扩展板有三极管或MOS管驱动输入，可接GPIO8；裸蜂鸣器不得直接由GPIO供电。

## 正式电源

当前正式供电尚未实测，计划如下：

```text
12V电池
  +-- DCDC 1: 5V   -> ESP32 5V/VIN、扩展板
  |          3.3V -> TFT VCC
  +-- DCDC 2: 5V   -> Anchor 0、Anchor 1

所有输出GND在低阻公共点汇接
```

Tag侧：

```text
带保护锂电池 -> 带开关5V升压DCDC -> Tag BU03-Kit 5V
```

两路DCDC必须空载调整并测量合格后再接模块。5V允许4.9到5.1V，3.3V允许3.25到3.35V。USB调试时断开外部5V。UWB电源必须使用焊接或锁紧端子，不使用漆包线缠绕供电。

## 射频布局

- 两Anchor天线中心同高、同一朝向并保持左右对称。
- Tag测试姿态固定，天线不要被手掌、身体、电池或金属遮挡。
- DCDC、电池、蜂鸣器、TFT排线和大电流回路远离UWB天线。
- 最终测量的是天线相位中心坐标，不是PCB外壳边缘。

