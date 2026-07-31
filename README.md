# C题门锁固件：BU03-Kit采集、定位与控制

当前工程已经打通BU03 UART2距离帧和主USB真实Tag ID到门锁判决输出的软件链：双基站前向二维定位、距离与角度滤波、ID认证、区域状态机、拨码/LED/蜂鸣器输出和2.4英寸SPI TFT实屏驱动。距离或身份链路任一超过500ms无合法帧，系统自动回到安全闭锁状态。

`host/`提供用于精度分析的Python桌面上位机，可同时观察原始、校正和滤波距离、二维轨迹、定点统计及线性标定结果。

新队员或新的Codex会话请先阅读[HANDOFF.md](HANDOFF.md)。其中记录了实物身份、已验证功能、当前标定、未解决风险和后续工作，不能只根据本README判断作品已经验收完成。

## 默认接线

```text
Anchor 0 PA2/UART2_TX -> ESP32-S3 GPIO18（UART1 RX）
Anchor 0 GND          -> ESP32-S3 GND
Anchor 0 主USB D-     -> ESP32-S3 GPIO19（原生USB D-）
Anchor 0 主USB D+     -> ESP32-S3 GPIO20（原生USB D+）
Anchor 0 主USB VBUS   -> 独立稳定5V电源
```

固件禁用UART TX，只接收Anchor 0测距数据；同时以USB Host只读Anchor 0完整TWR帧中的Tagid。GPIO18用于UWB距离RX，GPIO19/20专用于USB D-/D+。

数字钥匙是唯一移动Tag；门锁内只配置Anchor 0和Anchor 1。ESP32-S3只连接Anchor 0的UART2和主USB，Anchor 1只需可靠供电，两块Anchor的TX不能并联。详细供电与USB数据线接法见`docs/HARDWARE.md`。

## 构建

本机ESP-IDF 5.5.4实际使用Python 3.11虚拟环境，直接双击或在终端运行：

```powershell
.\build_idf.bat
```

脚本只为当前命令设置环境变量，不修改Windows全局环境。

也可以在已经初始化的ESP-IDF 5.5.4命令行中执行：

```powershell
cd <仓库目录>
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

GPIO和波特率位于：

```text
Component config
  -> C题 BU03-Kit 串口诊断
```

烧录和监视：

```powershell
.\flash_monitor_idf.bat COM端口
```

退出监视器：`Ctrl+]`。

## 期望日志

文本协议会同时显示ASCII和十六进制：

```text
I (...) BU03_DIAG: RX frame=1 len=... reason=newline
I (...) BU03_DIAG: ASCII: ...
I (...) BU03_DIAG: 00000000  ...
I (...) C_KEY: UWB ok=... bad=... mask=0x03 A0=... A1=... mm
```

没有换行的二进制协议会在串口空闲50ms后切帧，只显示十六进制。

请保存连续10~20帧日志，并同时记录真实距离、Tag/Anchor ID、固件版本和配置。解析器处理UART2 37字节距离帧和主USB 101字节身份帧，两种协议均有真实硬件回归样本。

## 当前验证状态

- ESP-IDF版本：5.5.4。
- 目标芯片：ESP32-S3。
- 完整构建：通过。
- 固件：`build/c_key_door.bin`。
- 应用二进制大小：0x53bb0字节，约334.9KiB，应用分区剩余67%。
- 首次上板通过：ESP32-S3-N16R8识别为COM21（CH343），固件已烧录并通过SHA校验，启动日志确认门锁主循环、TFT SPI和GPIO18 UWB接收串口初始化成功。
- Anchor 0 UART2实物接收通过：约47帧/5秒，坏帧为0，有效掩码为0x03。
- Anchor 0主USB在PC端实测通过：VID:PID为0483:5740，3秒收到30个101字节帧，解析得到真实Tagid=0；ESP32-S3 USB Host固件已构建，实物D+/D-/VBUS接线和烧录后联调尚待完成。
- LVSN-TFT-2.4实屏通过：VCC接3.3V，ILI9341驱动下文字、颜色和方向正常。
- 四位拨码通过：全OFF为ID 00，bit0 ON为ID 01，屏幕实时更新。
- 闭锁红灯通过：屏幕`LOCK:CLOSED`时GPIO15红灯点亮。
- 双基站角度仍存在明显左右不对称，尚未满足最终验收要求，详见`HANDOFF.md`。
- 调试上位机10项测试和模拟数据界面启动通过；2026-07-31实物复测10秒收到100条诊断帧、解析错误0、有效掩码0x03。

## 核心算法单元测试

`components/c_key_core`已经包含：

- 双基站两圆交点定位，并固定选择门锁正前方解。
- 门锁圆柱中心距离到边界径向距离的换算。
- 以门锁正前方为0度的方位角计算。
- 两路测距的新鲜度和时间同步检查。
- 5点中值加EMA距离滤波。
- 4位ID认证、角度滞回、1m/2m距离滞回状态机。
- 迎宾开关、开锁和闭锁事件输出。
- 两路测距到最终门锁状态的一体化处理流水线。
- 每个Anchor独立距离偏置、重复帧抑制和定位残差门限。
- BU03 UART2帧头、长度、8路小端毫米距离、校验和、帧尾和流式重新同步。
- BU03主USB 101字节TWR帧、真实Tagid、XOR校验和流式重新同步。

在PC上执行：

```powershell
.\tests\run_host_tests.bat
```

该组件不依赖ESP-IDF，已通过PC GCC严格警告测试和ESP32-S3交叉编译。

test_contest_scenario.c会生成左右440 mm基线下的双Anchor理想测距，完整模拟钥匙从3 m进入1.75 m迎宾区、0.8 m开锁区，再跨越1 m和2 m离开。测试同时覆盖前后镜像选择、不可相交测距、1 m附近抖动滞回、50度角度拒绝、ID失配和信号丢失闭锁。

流水线入口定义在`c_key_pipeline.h`。UART2解析层提供Anchor 0和Anchor 1距离，主USB解析层提供真实Tag ID；输出直接给出滤波距离、二维位置、径向距离、方位角、门锁状态和动作事件。

## 距离与真实ID到门锁状态的完整链路

主程序通过`c_key_bu03_bridge`把UART2的A0/A1两路毫米距离送入定位流水线，通过`c_key_bu03_usb`从Anchor 0主USB完整帧取得Tagid。两条链路均新鲜时才判定钥匙存在；否则TFT钥匙ID显示`----`并闭锁。运行时每秒输出状态，进入或离开区域、开闭锁以及链路恢复或超时会立即输出。

定位几何和标定参数位于：

~~~text
Component config
  -> C题 BU03-Kit 串口诊断
    -> C题 双基站定位几何与标定
~~~

可配置两块Anchor的X/Y坐标、两路距离零偏、圆柱半径、正前方角度修正、EMA系数和最大定位残差。当前A0=(-220,0) mm、A1=(220,0) mm，即天线中心间距440mm；门锁正前方为+Y，圆柱半径为300mm。

## 拨码、LED和蜂鸣器GPIO层

`components/c_key_io`已实现4位拨码去抖、闭锁红灯、开锁绿灯、迎宾灯和非阻塞蜂鸣器脉冲。

根据ESP32-S3-N16R8照片，默认拨码GPIO为4/5/6/7，蜂鸣器为8，红/绿/迎宾LED为15/16/17。可在以下菜单中修改：

```text
Component config
  -> C题 门锁GPIO配置
```

拨码推荐每位使用10k上拉、开关接地；开关拨到ON时读作二进制1。初始化、故障和ID失配状态默认闭锁，红灯亮、绿灯灭、迎宾灯灭、蜂鸣器关闭。

## 实测遥测日志

固件每秒以及每次状态变化时输出一条C_KEY_CSV记录，字段依次为时间、钥匙ID、拨码ID、UWB链路、有效掩码、A0/A1原始距离、协议保留A2距离、定位有效位、X/Y、径向距离、方位角、残差、状态和事件位。

将idf.py monitor或串口工具的完整输出保存为monitor.log，然后执行：

~~~powershell
.\tools\extract_telemetry.ps1 .\monitor.log .\telemetry.csv
~~~

提取器只保留C_KEY_CSV行，校验每行字段数并生成可直接导入Excel的UTF-8 CSV。tools/testdata/monitor_example.log用于脚本回归测试。

## 定位调试上位机

新固件每个合法UWB帧输出一行`C_KEY_DIAG_V1`，原`C_KEY_CSV`保持不变。首次使用：

~~~powershell
cd host
.\setup.bat
.\run.bat
~~~

无硬件演示：

~~~powershell
.\run.bat --demo
~~~

详细操作、定点采集和CSV回放见`host/README.md`。

## 两路距离零偏标定

复制tools/calibration_measurements.csv并填写测量记录。每块Anchor至少采集3组，建议在1 m、1.5 m、2 m和3 m附近各静止记录20帧后取中位数。

CSV格式：

~~~csv
sample,anchor_id,true_mm,measured_mm
P1,0,1000,1080
~~~

运行：

~~~powershell
.\tools\calibrate_ranges.ps1 .\tools\calibration_measurements.csv
~~~

脚本输出Anchor 0和Anchor 1的建议零偏、校正后平均绝对误差、最大误差和标准差，并生成可填写到menuconfig中的CONFIG_C_KEY_ANCHOR0/1_OFFSET_MM值。tools/testdata/calibration_example.csv是脚本自测样例，不得当作实物标定结果。

## TFT显示内容模型

c_key_display_format()把定位流水线结果统一转换成6行定长文本：

~~~text
KEY:05 LOCK:05 OK
D: 1.50m A: +8.0deg
X:+0.25 Y:+1.80
ZONE:WELCOME
LOCK:CLOSED WELCOME:ON
UWB:OK RES:0.03m
~~~

无有效定位时，距离、角度、坐标和残差显示占位符，不会继续显示上一次的旧数据。components/c_key_tft已实现320x240横屏显示、ILI9341/ST7789可切换初始化、LCD ID读取和6行文本绘制。默认SPI引脚为SDI=11、CLK=12、SDO=13、CS=10、D/C=9、BLK=14。

## 后续工作

当前优先事项是完成Anchor 0主USB到ESP32-S3 GPIO19/20的实物联调、确认蜂鸣器硬件驱动、完成正式供电、改善左右角度不对称，并在最终机械安装后重新标定。完整状态和验收清单见：

- [交接说明](HANDOFF.md)
- [硬件与接线](docs/HARDWARE.md)
- [BU03串口协议](docs/BU03_PROTOCOL.md)
- [标定与验收](docs/CALIBRATION_AND_ACCEPTANCE.md)

正式供电首次上电仍应从低压分路开始，禁止电脑USB与外部5V同时给ESP32供电。
