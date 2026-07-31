# C题数字钥匙：BU03标签 + BU04 PDOA门锁

本工程实现电赛 C 题“基于无线通信的数字钥匙实验系统”。当前主方案使用一块
BU03-Kit 作为数字钥匙标签，一块 BU04-Kit 作为门锁 PDOA 基站，ESP32-S3
通过 BU04 UART2 同时取得标签身份、距离和方位角。

旧的“双 BU03 TWR”版本已保存为标签 twr-direct-range-20260731，当前开发分支为
pdoa-bu03-bu04。

## 当前数据链

~~~text
BU03 Tag（短地址0x6E19）
    <-------- UWB -------->
BU04 PDOA Base
    UART2_TX（31字节Hex帧）
         -> ESP32-S3 GPIO18
         -> 标签地址映射为逻辑ID
         -> 距离/角度校正和滤波
         -> ID认证、区域状态机
         -> TFT、LED、蜂鸣器、诊断串口
~~~

门锁使用标签到 BU04 天线中心的距离和 PDOA 角度换算二维坐标。区域判定使用
标签到门锁圆柱外壳的距离，即中心距离减去 0.30 m。

## 当前端口

端口会随插拔变化，2026-07-31 实测为：

| 设备接口 | 当前端口 | 用途 |
|---|---:|---|
| BU04 AT/烧写口 | COM14 | 配置与配对 |
| BU04 USB测距数据口 | COM25 | PC抓取31字节二进制帧 |
| ESP32-S3 CH343 COM口 | COM21 | 烧录、日志、上位机 |

上位机必须连接 ESP32 诊断串口，不能直接连接 COM25。正式整机由 BU04
UART2_TX 接 ESP32 GPIO18。

## 构建与测试

~~~powershell
.\tests\run_host_tests.bat
.\build_idf.bat
~~~

2026-07-31 已通过全部 C 核心测试和 ESP-IDF 5.5.4 完整构建。生成镜像：

~~~text
build/c_key_door.bin
大小 0x43d00 字节，应用分区剩余 74%
~~~

烧录时先确认 ESP32 的实际端口：

~~~powershell
.\flash_idf.bat COMxx
~~~

不要将 COM25 作为烧录端口。

## 上位机

~~~powershell
cd host
.\setup.bat
.\run.bat
~~~

上位机读取 C_KEY_DIAG_V2，显示：

- BU04 原始距离与角度；
- 校正、滤波后的距离和角度；
- X/Y、门锁边界距离和区域状态；
- 标签地址、逻辑ID、拨码ID和帧质量；
- 定点采集、距离线性标定、角度误差统计、CSV记录与回放。

详细说明见 [host/README.md](host/README.md)。

## 默认接线

~~~text
BU04 UART2_TX（Kit排针4） -> ESP32 GPIO18
BU04 GND                  -> ESP32 GND
BU04 5V                   -> 独立5V DCDC
~~~

ESP32 只接收 BU04 数据，UART TX 默认禁用。TFT 只能使用 3.3V 供电。完整 GPIO、
电源和射频布局见 [硬件与接线](docs/HARDWARE.md)。

## 配置

menuconfig 路径：

~~~text
Component config
  -> C题 BU04 PDOA数字钥匙
~~~

关键默认值：

~~~text
BU04 UART RX GPIO = 18
BU04 baud = 115200
标签短地址 = 0x6E19
逻辑钥匙ID = 0
门锁半径 = 300 mm
距离 scale = 1000000 ppm
距离 offset = 0 mm
角度零偏 = 0 deg
~~~

## 验证状态

已完成：

- BU03 标签与 BU04 基站配对；
- BU04 Hex 输出和真实 COM25 帧解析；
- 标签地址、距离、角度、校验和及流式重同步；
- PDOA 距离/角度到定位、认证和状态机的软件链；
- 中文 TFT、拨码、LED、低电平蜂鸣器既有驱动集成；
- C 核心测试、Python 上位机 13 项测试、ESP32 完整构建。

待完成：

- 正式供电、装箱后的距离和角度标定；
- 动态进出、ID失配、掉线、重启和 30 分钟老化验收。

2026-08-01实机联调：COM21烧录和SHA校验成功；BU04 UART2接入后8秒收到356条
C_KEY_DIAG_V2，解析错误0，标签地址、距离、角度、坐标和状态机均有效。

接手开发先阅读 [HANDOFF.md](HANDOFF.md) 和
[标定与验收](docs/CALIBRATION_AND_ACCEPTANCE.md)。

协议实现与实测帧见 [BU04 PDOA协议](docs/BU04_PDOA_PROTOCOL.md)。
