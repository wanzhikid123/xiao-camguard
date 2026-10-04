> **2026-10-04 已确认实施范围**：5 秒至 7 天，默认 15 分钟并立即首拍；每次启动独立 Session，保留当前照片尺寸和质量；单张下载与确认整组删除；重启停止。与事件监测互斥，必须先停止另一个；录像跳过，短占用最多重试 30 秒且不跨下一周期；五次连续真实失败或空间不足停止。无 ZIP、日程、自动视频合成或重启续拍。具体行为及实际验收结果见 [实施记录](IMPLEMENTATION_SUMMARY.md) 和 [功能说明](docs/FEATURES_BLE_TIMELAPSE_MP4.md)。下面保留原始计划供追溯。

> **最新验收约束**：用户要求跳过需要等待十分钟以上的测试，接受几分钟检查。六小时连续测试已按要求停止，五分钟检查通过；原文六小时门槛不再要求等待完成，亦不标记为通过。

# Phase 8：定时拍照 / Timelapse Photo

## 101. 功能目标

给现有 XIAO ESP32-S3 Sense 摄像头项目增加：

```text
定时自动拍照
```

用户可以设置一个拍照间隔，例如：

```text
1 分钟
5 分钟
15 分钟
30 分钟
1 小时
6 小时
12 小时
24 小时
```

也允许自定义时间。

设备按照设定间隔自动拍摄 JPEG 照片。

所有定时拍摄的照片不要散落在普通照片目录中，而是按照一次任务 / 一天 / 一个 Timelapse Session 存放在独立文件夹中。

例如：

```text
/timelapse/
└── TL_20261004_120000/
    ├── IMG_20261004_120000.jpg
    ├── IMG_20261004_121500.jpg
    ├── IMG_20261004_123000.jpg
    ├── IMG_20261004_124500.jpg
    └── IMG_20261004_130000.jpg
```

这样以后：

```text
一次定时拍摄任务
=
一个文件夹
```

方便：

```text
浏览
下载
删除
备份
后续制作延时摄影视频
```

---

# 102. 功能名称

Web UI 中建议命名：

```text
Timelapse
```

或者：

```text
Interval Photography
```

推荐最终显示：

```text
Timelapse / Interval Photos
```

---

# 103. 基本工作流程

例如用户设置：

```text
Interval:
15 minutes
```

点击：

```text
Start Timelapse
```

设备：

```text
创建 Session
↓
创建独立目录
↓
等待 / 拍第一张
↓
每 15 分钟拍一张
↓
保存 JPEG
↓
等待下一次
↓
持续运行
```

直到：

```text
用户 Stop
设备重启
达到设置的最大照片数
达到结束时间
SD 卡空间不足
发生严重 Camera/SD 错误
```

---

# 104. Session 文件夹

每次用户点击：

```text
Start Timelapse
```

创建一个新的 Session 文件夹。

推荐：

```text
/timelapse/TL_YYYYMMDD_HHMMSS/
```

例如：

```text
/timelapse/TL_20261004_143000/
```

里面的照片：

```text
IMG_20261004_143000.jpg
IMG_20261004_144500.jpg
IMG_20261004_150000.jpg
IMG_20261004_151500.jpg
```

---

# 105. 为什么使用独立 Session 文件夹

不要把所有 Timelapse 照片直接放到：

```text
/photos/
```

否则几个月后可能出现几万张照片，难以管理。

正确结构：

```text
/photos/
    普通手动拍照

/videos/
    AVI / MP4

/timelapse/
    TL_20261004_080000/
    TL_20261004_150000/
    TL_20261005_090000/
```

这样普通拍照和自动定时拍照完全分开。

---

# 106. 可选：按日期继续分组

如果以后长期运行，也可以：

```text
/timelapse/
└── 2026-10-04/
    ├── TL_080000/
    └── TL_150000/
```

但第一版不需要这么复杂。

第一版推荐：

```text
/timelapse/TL_YYYYMMDD_HHMMSS/
```

---

# 107. 时间来源

Timelapse 强依赖准确时间。

优先时间来源：

```text
NTP
```

Wi-Fi 连接后同步。

需要维护：

```text
timeValid
```

如果：

```text
NTP 已同步
```

文件名使用真实时间。

例如：

```text
IMG_20261004_141500.jpg
```

如果没有时间：

不要生成错误日期，例如：

```text
19700101
```

应该使用：

```text
IMG_000001.jpg
IMG_000002.jpg
```

直到时间恢复。

---

# 108. 时区

使用项目设置的本地时区。

建议默认：

```text
Europe/Berlin
```

但不要硬编码。

设置项：

```text
timezone
```

Timelapse 文件名使用：

```text
Local Time
```

而内部调度最好使用：

```text
monotonic timer
```

避免：

```text
夏令时变化
NTP 时间修正
```

导致拍摄间隔混乱。

---

# 109. 拍摄间隔

Web UI 提供 Preset：

```text
10 seconds
30 seconds
1 minute
5 minutes
15 minutes
30 minutes
1 hour
2 hours
6 hours
12 hours
24 hours
Custom
```

虽然主要目标是：

```text
1 minute
15 minutes
1 hour
```

但多几个预设不会增加太多复杂度。

---

# 110. 自定义间隔

允许输入：

```text
Value
+
Unit
```

例如：

```text
3
Minutes
```

或者：

```text
90
Seconds
```

支持：

```text
Seconds
Minutes
Hours
```

内部统一转换为：

```text
interval_ms
```

或者：

```text
interval_sec
```

推荐：

```text
uint64_t intervalMs
```

---

# 111. 最小间隔限制

不要允许：

```text
0 秒
```

或者非常短的：

```text
100 ms
```

第一版建议最小：

```text
5 seconds
```

如果 Camera + SD 性能不够，可以改成：

```text
10 seconds
```

Web UI 做输入检查。

---

# 112. 最大间隔

支持至少：

```text
24 hours
```

推荐允许：

```text
7 days
```

使用：

```text
uint64_t
```

避免时间溢出。

---

# 113. 拍第一张照片的行为

增加选项：

```text
Take first photo immediately
```

默认：

```text
ON
```

例如：

```text
Start 14:00
Interval 15 min
```

默认：

```text
14:00 photo
14:15 photo
14:30 photo
14:45 photo
```

而不是等到：

```text
14:15
```

才拍第一张。

---

# 114. 两种时间模式

建议以后支持：

### Relative Interval

```text
每隔 15 分钟
```

例如：

```text
14:03
14:18
14:33
14:48
```

---

### Clock Aligned

例如设置：

```text
Every 15 minutes
```

拍摄：

```text
14:00
14:15
14:30
14:45
15:00
```

第一版只做：

```text
Relative Interval
```

以后再增加 Clock Aligned。

---

# 115. Timelapse 状态

定义：

```cpp
enum class TimelapseState {
    DISABLED,
    IDLE,
    RUNNING,
    CAPTURING,
    PAUSED,
    ERROR
};
```

---

# 116. 状态数据

维护：

```text
enabled
running
sessionName
sessionPath

intervalMs

startTime
lastCaptureTime
nextCaptureTime

photoCount
failedCount

lastPhotoPath
lastError
```

---

# 117. Timelapse Manager

创建独立模块：

```text
src/timelapse/
├── timelapse_manager.cpp
└── timelapse_manager.h
```

不要将逻辑直接塞入：

```text
main.cpp
```

---

# 118. API

建议：

```cpp
bool timelapseInit();

bool timelapseStart(
    uint64_t intervalMs
);

void timelapseStop();

void timelapsePause();

void timelapseResume();

void timelapseLoop();

bool timelapseIsRunning();

String timelapseGetSessionPath();

uint32_t timelapseGetPhotoCount();

uint64_t timelapseGetNextCaptureTime();
```

---

# 119. 不要阻塞

禁止：

```cpp
delay(900000);
```

来实现：

```text
15 分钟等待
```

必须采用：

```text
millis()
FreeRTOS timer
esp_timer
```

这类非阻塞方式。

设备等待期间：

```text
Web UI
BLE
Camera
Wi-Fi
```

仍然正常运行。

---

# 120. 推荐调度方式

第一版推荐：

```text
esp_timer
```

或者项目现有的稳定 scheduler。

逻辑：

```text
if now >= nextCaptureTime
    requestCapture
```

不要直接在 Timer callback 里面拍照。

Timer callback：

```text
只发 event
```

真正拍照交给：

```text
Camera Task
```

---

# 121. Camera 资源协调

Timelapse 不能和：

```text
录像
手动拍照
Live stream
```

同时随意抢 Camera。

Camera Manager 应该有统一资源锁。

例如：

```text
CameraOwner
```

可能状态：

```text
NONE
PHOTO
VIDEO
STREAM
TIMELAPSE
```

---

# 122. 冲突策略

假设 Timelapse 到点：

```text
15:00
```

但正在录像。

不要：

```text
强制停止录像
```

推荐：

```text
Skip
```

并记录：

```text
[TL] Capture skipped: camera busy
```

或者：

```text
Retry for a short window
```

第一版推荐：

```text
Retry up to 30 seconds
```

如果 30 秒仍忙：

```text
Skip this frame
```

等待下一次间隔。

---

# 123. 不要延迟整个时间线

例如：

```text
计划：
14:00
14:15
14:30
```

14:15 那张因为 Camera busy，实际 14:15:20 才拍。

下一张仍然应该大致：

```text
14:30
```

不要变成：

```text
14:30:20
```

否则长期运行会不断漂移。

---

# 124. 照片格式

第一版：

```text
JPEG
```

不要使用：

```text
BMP
RAW
PNG
```

理由：

```text
空间小
Camera 原生输出
保存速度快
兼容性高
```

---

# 125. JPEG Quality

复用现有 Camera 设置。

或者 Timelapse 单独设置：

```text
Timelapse JPEG Quality
```

建议第一版：

```text
使用当前 Camera JPEG quality
```

避免配置过多。

---

# 126. Resolution

Timelapse 可以选择：

```text
Use Current Camera Resolution
```

默认：

```text
ON
```

以后可增加：

```text
Timelapse Resolution
```

例如：

```text
320x240
640x480
800x600
1280x1024
```

但第一版不要重复一套 Camera Settings。

---

# 127. 文件名

有准确 RTC/NTP 时间：

```text
IMG_YYYYMMDD_HHMMSS.jpg
```

例如：

```text
IMG_20261004_151500.jpg
```

如果同一秒出现重复：

```text
IMG_20261004_151500_01.jpg
IMG_20261004_151500_02.jpg
```

---

# 128. 无时间时文件名

如果：

```text
timeValid == false
```

使用：

```text
IMG_000001.jpg
IMG_000002.jpg
IMG_000003.jpg
```

不要覆盖已有文件。

---

# 129. Session Metadata

每个 Session 目录建议增加：

```text
session.json
```

例如：

```json
{
  "type": "timelapse",
  "start_time": "2026-10-04T14:00:00+02:00",
  "interval_seconds": 900,
  "resolution": "640x480",
  "jpeg_quality": 12,
  "photo_count": 24
}
```

用途：

```text
Web UI 展示
以后生成视频
恢复 Session 信息
```

---

# 130. session.json 更新策略

不要每拍一张照片就完整重写大量文件。

可以：

```text
每 10 张
```

或者：

```text
Stop 时
```

更新一次。

如果实现简单：

每张更新也可以，但注意 SD 写入寿命和性能。

---

# 131. Session Status

session.json 可以记录：

```text
status
```

例如：

```text
RUNNING
COMPLETED
STOPPED
ERROR
```

---

# 132. 用户停止

点击：

```text
Stop Timelapse
```

流程：

```text
取消下一次 timer
↓
等待当前照片保存完成
↓
更新 session.json
↓
Status = COMPLETED / STOPPED
↓
释放 Camera resource
```

不要在 JPEG 写到一半时强行关文件。

---

# 133. Web UI

新增页面：

```text
Timelapse
```

显示：

```text
Timelapse
────────────────────────

Status:
Idle

Interval:
[ 15 ] [ Minutes ▼ ]

Preset:
[1 min]
[5 min]
[15 min]
[30 min]
[1 hour]

Take first photo immediately
[✓]

[ Start Timelapse ]
```

运行以后：

```text
Status:
Running

Session:
TL_20261004_140000

Interval:
15 min

Photos:
12

Last Capture:
16:45:00

Next Capture:
17:00:00

Storage:
13.2 GB free

[ Stop ]
```

---

# 134. Session 浏览

Web UI 增加：

```text
Timelapse Sessions
```

例如：

```text
TL_20261004_140000
24 photos
384 MB
14:00 → 20:00
[Open] [Download] [Delete]

TL_20261003_090000
96 photos
1.2 GB
[Open] [Download] [Delete]
```

---

# 135. 打开 Session

点击：

```text
Open
```

显示照片缩略图：

```text
IMG_140000.jpg
IMG_141500.jpg
IMG_143000.jpg
IMG_144500.jpg
```

第一版如果做缩略图比较麻烦：

可以只显示：

```text
Filename
Size
Time
Download
```

不要为了缩略图阻塞主要功能。

---

# 136. Session 删除

点击：

```text
Delete Session
```

不要立即删除。

要求：

```text
Confirm Delete
```

确认后删除：

```text
整个 Session 文件夹
```

包括：

```text
JPG
session.json
```

---

# 137. 运行中的 Session 不能删除

如果：

```text
Session == currentSession
```

则：

```text
Delete Disabled
```

用户必须：

```text
Stop Timelapse
```

以后才能删除。

---

# 138. 下载

第一版支持：

```text
单张 JPEG 下载
```

以后可增加：

```text
Download Session as ZIP
```

但 ESP32 上实时打 ZIP 会增加 CPU 和 RAM 压力。

因此：

```text
ZIP
```

属于可选后续功能。

---

# 139. BLE Status

增加 BLE Characteristic：

```text
Timelapse Status
```

返回：

```text
IDLE
RUNNING
CAPTURING
PAUSED
ERROR
```

---

# 140. BLE 信息

另一个 Characteristic 可以返回：

```text
Interval
Photo Count
Next Capture
```

例如：

```text
interval=900;count=12;next=17:00:00
```

第一版 BLE 只读即可。

---

# 141. BLE 控制

以后可以支持：

```text
START
STOP
```

第一版不做。

Timelapse 配置优先使用：

```text
Web UI
```

---

# 142. 配置保存

NVS 中增加：

```text
timelapse_enabled
timelapse_interval
timelapse_first_immediate
```

但注意：

```text
running
```

是否跨重启恢复，需要单独设计。

---

# 143. 重启行为

第一版建议：

设备重启以后：

```text
Timelapse 不自动恢复
```

状态：

```text
STOPPED_BY_REBOOT
```

原因：

避免设备：

```text
意外重启
↓
没人知道
↓
又自动开始持续拍摄
```

以后可增加：

```text
Resume after reboot
```

选项。

---

# 144. 后续自动恢复

可选：

```text
Resume Timelapse After Reboot
```

如果开启：

NVS 保存：

```text
session path
interval
last scheduled capture
running
```

开机后：

```text
检查 Session
↓
恢复 Timer
↓
继续保存到原 Session
```

但第一版不做。

---

# 145. SD 空间保护

每次拍照前检查：

```text
Free Space
```

例如至少保留：

```text
100 MB
```

设置：

```text
timelapse_min_free_mb
```

默认：

```text
100
```

如果不足：

```text
Stop Timelapse
```

状态：

```text
ERROR_NO_SPACE
```

不要自动删除旧照片，除非用户以后明确开启该功能。

---

# 146. 文件数量限制

支持：

```text
Maximum Photos
```

例如：

```text
Unlimited
100
500
1000
5000
```

第一版默认：

```text
Unlimited
```

---

# 147. 拍摄时长限制

支持：

```text
Run Duration
```

例如：

```text
Unlimited
1 hour
6 hours
12 hours
24 hours
7 days
```

第一版可以不实现。

优先实现：

```text
手动 Start / Stop
```

---

# 148. 未来：结束时间

以后可设置：

```text
Start:
08:00

Stop:
18:00
```

例如白天每 15 分钟拍一张。

第一版不需要。

---

# 149. 未来：每天时间窗口

例如：

```text
每天
07:00 → 19:00

每 15 分钟
```

非常适合：

```text
植物
建筑工地
天气
窗外景色
```

但属于 Phase 8.2。

---

# 150. Timelapse 和 H.264 转码冲突

前面 Phase 7 有：

```text
Idle AVI → H.264
```

需要协调。

优先级：

```text
Timelapse Capture
>
H264 Transcoding
```

如果到了 Timelapse 拍摄时间：

```text
正在转码
↓
暂停 / Abort Transcoding
↓
拍照片
↓
转码以后重新等待 Idle
```

因为：

```text
抓拍时间准确性
```

比后台转码更重要。

---

# 151. Timelapse 和录像冲突

优先级：

```text
Active Video Recording
>
Timelapse
```

录像时不要为了拍 Timelapse：

```text
停止录像
```

如果 Camera 支持录像同时抓取 JPEG 而且验证稳定：

以后可以支持。

第一版：

```text
录像时跳过 Timelapse frame
```

---

# 152. Timelapse 和 Web Live View

如果 Live View 正在运行：

优先尝试：

```text
短暂 Capture
```

但如果现有 Camera 架构不能安全做到：

第一版：

```text
Stream active → skip / short retry
```

不要因为 Timelapse 导致 Web Stream 崩溃。

---

# 153. 拍摄失败

如果：

```text
camera capture failed
```

记录：

```text
failedCount++
```

不要结束整个 Session。

下一次继续尝试。

---

# 154. 连续失败保护

如果：

```text
连续 5 次失败
```

则：

```text
Timelapse State = ERROR
```

停止自动拍照。

Serial：

```text
[TL] Too many consecutive capture failures
```

防止设备永久错误循环。

---

# 155. SD 写入失败

如果：

```text
JPEG capture success
SD write fail
```

则：

```text
删除不完整文件
```

保留 Session。

下一次继续。

连续错误达到阈值后：

```text
ERROR
```

---

# 156. 写文件安全

推荐：

```text
IMG_xxx.tmp
```

写完：

```text
flush
close
rename
```

最终：

```text
IMG_xxx.jpg
```

避免断电留下看起来正常但损坏的 JPEG。

---

# 157. 照片计数

只有：

```text
JPEG 保存并 rename 成功
```

以后才：

```text
photoCount++
```

失败不计入。

---

# 158. 内存要求

一张照片完成后：

立即：

```text
释放 Camera frame buffer
```

不要保留：

```text
旧 JPEG
```

到下一次拍摄。

Timelapse 是长时间运行功能，因此特别要防止：

```text
memory leak
```

---

# 159. 长时间稳定性测试

至少测试：

```text
1 min interval
运行 6 小时
```

理论：

```text
360 photos
```

检查：

```text
内存是否下降
Wi-Fi 是否稳定
BLE 是否稳定
SD 是否稳定
文件数量是否正确
```

---

# 160. 加速测试模式

为了开发测试：

增加编译或 Debug 模式：

```text
Timelapse Test Interval:
5 seconds
```

这样不需要每次等 15 分钟。

正式 UI 仍可允许 5 秒，但测试使用时更方便。

---

# 161. 日志

建议：

```text
[TL] Starting session
[TL] Path: /timelapse/TL_20261004_140000
[TL] Interval: 900 sec

[TL] Capture #1
[TL] Saved IMG_20261004_140000.jpg

[TL] Next capture: 14:15:00

[TL] Capture #2
[TL] Saved IMG_20261004_141500.jpg
```

不要每秒打印：

```text
waiting...
```

---

# 162. 状态日志

发生跳过：

```text
[TL] Capture delayed: camera busy
```

最终跳过：

```text
[TL] Capture skipped: camera busy
```

---

# 163. Web UI 中显示下一张时间

例如：

```text
Next Photo
14:45:00

In
12m 43s
```

倒计时可以由浏览器 JavaScript 本地计算。

不要让 ESP32：

```text
每秒发送更新
```

---

# 164. Storage Estimate

可以显示简单估算：

例如：

```text
Average JPEG:
180 KB

Interval:
15 min

Approx per day:
96 photos
≈ 17 MB/day
```

估算公式：

```text
Photos/day =
86400 / interval_seconds
```

然后：

```text
Estimated/day =
photosPerDay × recentAverageJpegSize
```

---

# 165. 最近 JPEG 平均大小

维护最近：

```text
10 photos
```

平均大小：

```text
avgJpegSize
```

用于：

```text
Storage Estimate
```

---

# 166. 未来：自动做 Timelapse 视频

这些 Session 文件夹以后可以作为 Phase 9 的输入：

```text
/timelapse/TL_xxx/
├── IMG_0001.jpg
├── IMG_0002.jpg
├── IMG_0003.jpg
```

转换成：

```text
timelapse.mp4
```

例如：

```text
30 fps
```

则：

```text
900 张照片
=
30 秒视频
```

但：

```text
ESP32-S3 上 JPEG → H264
```

仍然属于 CPU 密集工作。

第一版只保存照片。

---

# 167. 未来视频制作建议

以后如果增加：

```text
Create Timelapse Video
```

必须：

```text
用户手动触发
```

或者：

```text
设备 Idle 时运行
```

不要在 Timelapse 正在拍摄时同时编码视频。

---

# 168. 推荐目录最终结构

最终 SD 卡：

```text
/
├── photos/
│   ├── IMG_xxx.jpg
│   └── ...
│
├── videos/
│   ├── VID_xxx.avi
│   ├── VID_xxx.mp4
│   └── ...
│
├── timelapse/
│   ├── TL_20261004_080000/
│   │   ├── session.json
│   │   ├── IMG_20261004_080000.jpg
│   │   ├── IMG_20261004_081500.jpg
│   │   └── ...
│   │
│   └── TL_20261005_090000/
│       ├── session.json
│       └── ...
│
└── config/
```

---

# 169. Phase 8.1 MVP

第一阶段只实现：

```text
Start
Stop

1 min
15 min
1 hour
Custom

创建独立 Session 文件夹

JPEG 保存

Photo Count

Last Capture

Next Capture
```

不要一开始做：

```text
schedule
daily window
auto resume
video generation
ZIP
```

---

# 170. MVP 验收

设置：

```text
Interval = 1 minute
```

Start：

```text
TL_20261004_120000
```

运行 5 分钟。

结果应该至少有：

```text
IMG_120000.jpg
IMG_120100.jpg
IMG_120200.jpg
IMG_120300.jpg
IMG_120400.jpg
IMG_120500.jpg
```

允许因为 Camera busy 有少量时间误差。

---

# 171. Phase 8.2

增加：

```text
Maximum Photos
Duration
Schedule
Daily Time Window
Resume After Reboot
```

---

# 172. Phase 8.3

增加：

```text
Session Browser
Thumbnail
Bulk Download
Session Delete
Storage Estimate
```

---

# 173. Phase 8.4

可选：

```text
Create Timelapse MP4
```

输入：

```text
Session JPG folder
```

输出：

```text
timelapse.mp4
```

---

# 174. 最终验收 A

```text
选择 1 minute
↓
Start
↓
创建新文件夹
↓
立即拍第一张
↓
之后每分钟拍一张
↓
所有图片都进入同一个 Session 文件夹
```

---

# 175. 最终验收 B

```text
选择 15 minutes
↓
运行几个周期
↓
Web UI 正确显示
Last Capture
Next Capture
Photo Count
```

---

# 176. 最终验收 C

```text
Timelapse Running
↓
开始录像
↓
Timelapse 不破坏录像
↓
必要时跳过本次照片
↓
下一周期继续
```

---

# 177. 最终验收 D

```text
Timelapse Running
↓
SD 卡空间不足
↓
停止拍照
↓
显示 ERROR_NO_SPACE
↓
已有照片全部保留
```

---

# 178. 最终验收 E

```text
设备 Web UI / BLE 正常使用
↓
后台 Timelapse 持续数小时
↓
无明显 memory leak
↓
无 watchdog reset
↓
Wi-Fi 不掉线
```

---

# 179. 最重要的设计原则

```text
Timelapse 是低频后台任务
```

不能影响：

```text
录像
Camera
Web
Wi-Fi
BLE
```

同时：

```text
每一次 Timelapse
=
一个独立 Session 文件夹
```

不要把几个月的照片全部塞入一个目录。

推荐最终用户体验：

```text
Timelapse
─────────────────

Interval
[15 Minutes]

Take first photo immediately
[✓]

[ Start ]

Current Session
TL_20261004_140000

Photos
26

Last Photo
20:15

Next Photo
20:30

[ Stop ]
```

这就是 Phase 8 第一版应该实现的核心功能。
