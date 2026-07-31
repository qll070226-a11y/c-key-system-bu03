# C题数字钥匙系统交接说明

更新日期：2026-07-31

## 1. 当前方案

- 数字钥匙：BU03-Kit Tag，长地址 0525006475676E19，短地址 0x6E19。
- 门锁定位：BU04-Kit PDOA Base，已配对上述标签。
- 主控：ESP32-S3-N16R8。
- 人机与执行：2.4英寸 SPI TFT、4位拨码、红/绿/迎宾 LED、低电平蜂鸣器。
- 数据链：BU04 UART2_TX -> ESP32 GPIO18，31字节二进制帧同时提供标签地址、
  厘米距离和整数角度。
- 身份：固件将短地址 0x6E19 映射为逻辑钥匙 ID 0；其他标签地址判为无效ID。

旧双 BU03 TWR 方案已用 Git 标签 twr-direct-range-20260731 保存。不要在当前
PDOA 分支继续使用 A0/A1 两圆定位参数。

## 2. 当前状态

已完成：

- BU03 标签配置为 PDOA Tag。
- BU04 配置为 PDOA Base，并只保留当前标签配对。
- BU04 输出切换为 Hex 并保存，实际 COM25 数据持续输出。
- 已解析真实31字节帧：帧头2A、长度1B、标签地址、角度、距离、校验和及帧尾23。
- 校验为字节2到28异或，实际帧回归测试通过。
- 固件完成 BU04 流式解析、标签地址认证、距离/角度滤波、极坐标转 X/Y、
  门锁外壳距离、状态机、TFT和 GPIO 集成。
- 遥测升级为 C_KEY_DIAG_V2。
- 上位机升级为 PDOA 距离/角度诊断，13项测试通过。
- C 核心测试通过。
- ESP-IDF 5.5.4 完整构建通过，镜像 build/c_key_door.bin 为0x43d00字节。

尚未完成：

- 当前 ESP32 COM 口未接入电脑，因此 PDOA 镜像尚未烧录。
- BU04 UART2_TX 到 ESP32 GPIO18 的实时整机链路尚待验证。
- 正式12V电池、两路 DCDC、最终洞洞板和装箱未验收。
- 最终距离比例/零偏和安装角度零偏尚未标定。
- 动态进出、ID失配、掉线、重启和30分钟老化未完成。

## 3. 当前端口与接口

| 设备 | 当前端口 | 说明 |
|---|---:|---|
| BU04 AT/烧写口 | COM14 | CH340，配置口 |
| BU04测距数据口 | COM25 | VID 0483:5740，二进制数据 |
| ESP32-S3 COM口 | 未接入 | 历史曾为COM21 |

端口会变化，不要只按编号判断设备。上位机连接 ESP32 COM 口，不连接 COM25。

BU04 排针：

~~~text
pin 4 UART2_TX -> ESP32 GPIO18
GND            -> ESP32 GND
pin 22 5V      -> 独立5V DCDC
~~~

## 4. 配置备份

关键审计文件：

- backups/bu03/com14_20260731_230515.json：BU03修改前。
- backups/bu03/pdoa_tag_com14_20260731_230536.json：BU03 PDOA Tag配置后。
- backups/bu04/com6_20260731_230133.json：BU04修改前。
- backups/bu04/pdoa_base_com6_20260731_230145.json：BU04 Base配置后。
- backups/bu04/pdoa_pair_com6_20260731_230825.json：配对当前标签后。
- backups/bu04/com14_20260731_232354.json：BU04重新枚举后的配置。

COM6 是 BU04 曾用配置口，不代表设备型号。backups/bu03/com6_20260731_225834.json
是早期放错目录的同一 BU04 只读备份，保留作审计，不作为 BU03 身份依据。

配置工具：

- tools/bu03_at_backup.py：只读备份。
- tools/bu03_pdoa_config.py：角色配置。
- tools/uwb_pdoa_pair.py：带身份检查的安全配对。
- tools/bu03_twr_restore.py：需要回退旧 TWR 方案时使用。

## 5. 软件结构

~~~text
main/app_main.c                 UART、超时、TFT和GPIO主循环
components/c_key_core/
  bu04_pdoa.c                  31字节PDOA协议解析
  c_key_pipeline.c             PDOA滤波、坐标、认证和状态机
  c_key_telemetry.c            C_KEY_DIAG_V2
components/c_key_io/           拨码、LED和低电平蜂鸣器
components/c_key_tft/          中文ILI9341/ST7789显示
host/                          Python PDOA诊断上位机
tests/                         C核心测试
backups/                       UWB AT配置审计
docs/                          接线、标定和厂家资料
~~~

## 6. 算法口径

BU04 原始距离单位为 cm，原始角度单位为 deg。当前流程：

~~~text
原始距离 -> scale/offset -> 5点中值 + EMA
原始角度 -> 安装零偏 -> 5点中值 + 自适应EMA
滤波距离和角度 -> X=r*sin(a), Y=r*cos(a)
中心距离 -> max(0, 中心距离-0.30m) -> 区域状态机
~~~

正前方为0度、+Y；右侧为正角和+X。当前距离 scale=1、offset=0、角度零偏=0，
这些都是待实测的初值。

区域滞回：

| 功能 | 进入 | 退出 |
|---|---:|---:|
| 开锁区 | 0.90m | 1.10m |
| 迎宾区 | 1.90m | 2.10m |
| 有效角度 | 绝对值43度 | 绝对值47度 |

## 7. 接手后的第一步

1. 用数据线接 ESP32 左侧丝印 COM 的 Type-C 口。
2. 通过设备管理器确认新增 CH343 端口，不要误用 COM25。
3. 运行：

~~~powershell
.\tests\run_host_tests.bat
.\build_idf.bat
.\flash_idf.bat COMxx
~~~

4. 接 BU04 UART2_TX 到 GPIO18并共地。
5. 在 ESP32 串口查看 pdoa_ok 增长、pdoa_bad 不增长。
6. 运行 host/check_serial.bat COMxx --seconds 8，确认 diagnostics>0。
7. 启动上位机采集0度和1/2/3m数据，再做±15/±30/±45度测试。

## 8. 资料入口

- [硬件与接线](docs/HARDWARE.md)
- [标定与验收](docs/CALIBRATION_AND_ACCEPTANCE.md)
- [上位机](host/README.md)
- [BU04 PDOA Hex协议](docs/BU04_PDOA_PROTOCOL.md)
- [旧BU03协议回退资料](docs/BU03_PROTOCOL.md)
- [厂家资料](docs/reference/README.md)
