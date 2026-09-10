# BU04 PDOA 标定与验收

## 1. 坐标和判区口径

- BU04 双天线中心为原点。
- BU04 正前方为 +Y 和 0度。
- 面向正前方时，右侧为 +X 和正角，左侧为负角。
- BU04 输出距离是标签到天线中心的空间距离。
- 门锁判区距离为 max(0, 中心距离 - 0.30m)。

因此 TFT 显示的径向距离比 BU04 原始中心距离小30cm是设计要求，不是测距误差。

## 2. 滤波链

~~~text
距离cm -> mm -> scale/offset -> 5点中值 -> EMA(alpha 0.35)
角度deg -> 安装零偏 -> 7点环形Hampel异常值剔除
         -> One Euro(min_cutoff 0.8Hz, beta 0.03, d_cutoff 1.0Hz)
~~~

滤波能降低随机抖动，不能修复固定安装偏角、人体遮挡或地面/金属多径。
Hampel最小异常阈值为12度，连续拒绝最多3帧；第4帧仍指向新方向时重建历史，
以兼顾孤立跳点抑制和真实移动跟随。

上电或断流恢复后先积累5个连续有效样本，再向状态机提供定位结果。区域和
有效角度的改变还必须连续4帧一致才会生效；断流、坏帧和身份不匹配仍立即闭锁。
按实测约40~48Hz帧率，从恢复接收至允许状态变化通常约0.15~0.20秒。

## 3. 上位机定点采集

上位机连接 ESP32 诊断串口，不能连接 BU04 的 COM25。每个测点：

1. 固定 BU04、标签、外壳、电池和线束。
2. 输入标签相对 BU04 天线中心的真实 X/Y。
3. 分别输入 BU04 和标签天线中心离地高度。
4. 采集至少300帧；正式记录建议1000帧。
5. 保存环境照片、卷尺读数、标签姿态和 CSV。

上位机参考空间距离：

~~~text
sqrt(X² + Y² + (标签高度 - 基站高度)²)
~~~

固件当前不做高度分解，实时判区直接使用 BU04 空间距离。

正式验收不要只看上位机曲线。摆好测点并人工确认TFT后，关闭GUI避免占用串口，
运行静态验收工具：

~~~powershell
.\host\.venv\Scripts\python.exe tools\verify_static_position.py COM21 `
  --boundary-distance-m 1.0 --angle-deg 0 --display-confirmed
~~~

`--boundary-distance-m`必须填写标签定位点到门锁60cm圆柱边界的卷尺距离，不能填
到BU04天线中心的距离。工具会跳过启动预热，至少采集300帧，检查身份、定位有效率、
帧率、断流、距离误差、角度误差和TFT确认，并在`reports/static/`生成JSON和Markdown。

建议依次执行：

~~~powershell
# 0度方向距离验收
.\host\.venv\Scripts\python.exe tools\verify_static_position.py COM21 --boundary-distance-m 1.0 --angle-deg 0 --display-confirmed
.\host\.venv\Scripts\python.exe tools\verify_static_position.py COM21 --boundary-distance-m 2.0 --angle-deg 0 --display-confirmed
.\host\.venv\Scripts\python.exe tools\verify_static_position.py COM21 --boundary-distance-m 3.0 --angle-deg 0 --display-confirmed --expected-state SENSING

# 2m处角度验收，另外重复angle-deg 30、-30、45、-45
.\host\.venv\Scripts\python.exe tools\verify_static_position.py COM21 --boundary-distance-m 2.0 --angle-deg 0 --display-confirmed
~~~

1m和2m正好位于区域边界，受进出方向与滞回影响，静态定位报告不强制指定状态；
区域判决必须在后续动态进出测试中单独验收。

## 4. 距离标定

先在0度中心线采集1m、1.5m、2m、3m，至少使用三个不同距离。点击“计算标定”
得到：

~~~text
corrected_mm = raw_mm * scale + offset_mm
~~~

将结果写入 sdkconfig.defaults：

~~~text
CONFIG_C_KEY_PDOA_DISTANCE_SCALE_PPM=scale*1000000
CONFIG_C_KEY_PDOA_DISTANCE_OFFSET_MM=offset_mm
~~~

拟合点不能作为最终验收点。重新烧录后在未参与拟合的距离独立复测。

## 5. 角度标定

BU04 直接输出 PDOA 角度，不再由两段距离相减计算。推荐测点：

| 距离 | 角度 |
|---:|---|
| 1.5m | 0、±15、±30、±45度 |
| 2.0m | 0、±30、±45度 |
| 3.0m | 0、±30度 |

对每个点记录原始方位角和滤波方位角的中位数、标准差、MAD及绝对误差。

若所有点近似相差同一常数，可将安装零偏写入：

~~~text
CONFIG_C_KEY_FRONT_OFFSET_DEG=<BU04读数在真实0度时的中位数>
~~~

固件计算逻辑角度为“原始角度 - 零偏”。如果左右误差不同或误差随距离变化，
禁止用单一零偏硬凑；应先处理天线姿态、地面多径、遮挡和附近金属。

## 6. 区域逻辑

| 功能 | 进入阈值 | 退出阈值 |
|---|---:|---:|
| 开锁区 | 0.95m | 1.05m |
| 迎宾区 | 1.95m | 2.05m |
| 有效角度 | 绝对值43度 | 绝对值47度 |

只有标签地址匹配、逻辑ID与拨码ID一致、帧有效且角度合格时才允许迎宾和开锁。
表中阈值采用4帧确认，单帧越界不会改变灯光或门锁状态。
500ms没有合法 PDOA 帧时强制闭锁。

## 7. 必做验收

- [ ] 1、2、3m静态径向距离误差满足题目要求。
- [ ] 0、±30、±45度方位角误差绝对值不超过10度。
- [ ] 从3m外向内移动，感应、迎宾、开锁顺序正确。
- [ ] 从开锁区向外移动，闭锁和迎宾关闭顺序正确且不反复。
- [ ] 拨码ID改为非0后，当前0号钥匙立即失效并闭锁。
- [ ] 改回ID0后恢复识别。
- [ ] 标签断电或 BU04 数据线断开后500ms内 NO_KEY并闭锁。
- [ ] 未配对标签不能通过身份认证。
- [ ] 重启默认闭锁，不误开锁、不误鸣。
- [ ] 正式电池供电和USB调试分别测试，无反向灌电。
- [ ] 连续运行30分钟，无复位、花屏、异常发热或持续丢帧。

每轮验收保存日期、固件 Git 提交、sdkconfig、机械布局照片和上位机 CSV。
静态点同时保存`tools/verify_static_position.py`生成的JSON和Markdown报告。
