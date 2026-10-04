> **2026-10-04 已确认实施范围**：默认开启，可手动关闭；保留原分辨率、帧数、时长和声音；合格 MP4 发布后立即删除 AVI，即使 MP4 更大。不支持的尺寸保留原文件。原计划中的保留 AVI、降分辨率、降帧率建议已被本轮确认取代。具体行为及实际验收结果见 [实施记录](IMPLEMENTATION_SUMMARY.md) 和 [功能说明](docs/FEATURES_BLE_TIMELAPSE_MP4.md)。下面保留原始计划供追溯。

> **最新验收约束**：用户要求跳过需要等待十分钟以上的测试，接受几分钟检查。已完成的长片实测保留证据，不重跑；最新验收状态见实施记录。

# Phase 7：Idle AVI → H.264 MP4 后台转码

## 42. 目标

在 XIAO ESP32-S3 Sense 空闲时，将已经保存到 microSD 的 MJPEG AVI 视频后台转换为：

```text
H.264 + MP4
```

目的：

- 降低视频文件体积
- 提升播放器兼容性
- 方便手机、电脑、NAS 和浏览器后续处理
- 不影响正常拍照、录像、Web UI 和 BLE 管理

该功能必须是：

```text
OPTIONAL
```

默认允许用户在 Web UI 中开启或关闭。

---

# 43. 总体原则

录像优先级必须始终高于后台转码。

优先级：

```text
Camera Recording
    ↓
Live Preview / Web Stream
    ↓
Photo Capture
    ↓
BLE / Web Commands
    ↓
Idle Transcoding
```

后台转码绝对不能导致：

```text
Camera frame drop
录像中断
SD 写入失败
Web UI 卡死
Watchdog reset
内存耗尽
```

---

# 44. 输入格式

假设当前录像格式：

```text
AVI container
+
MJPEG video
```

即：

```text
AVI
├── JPEG frame 1
├── JPEG frame 2
├── JPEG frame 3
└── ...
```

Codex 首先必须确认当前项目实际 AVI 格式。

不要假设一定是标准 MJPEG AVI。

必须检查：

```text
AVI FourCC
frame layout
fps
resolution
JPEG frame extraction method
AVI index
```

如果现有 AVI writer 是项目自己实现的，则优先复用现有 frame / index 逻辑。

---

# 45. 输出格式

最终输出：

```text
MP4 container
+
H.264 video
```

例如：

```text
VID_20261004_112315.avi
```

转换为：

```text
VID_20261004_112315.mp4
```

不要：

```text
H.264 inside AVI
```

因为本功能的目标之一就是提升兼容性。

---

# 46. 转码流水线

整体流程：

```text
AVI
 ↓
读取一个 JPEG frame
 ↓
JPEG Decode
 ↓
RGB / YUV raw frame
 ↓
convert pixel format if needed
 ↓
H.264 Encode
 ↓
H.264 NAL units
 ↓
MP4 muxer
 ↓
.tmp.mp4
```

不要一次把整个 AVI 加载到 RAM。

必须：

```text
逐帧处理
```

---

# 47. 内存策略

ESP32-S3 RAM 和 PSRAM 很有限。

禁止：

```text
一次读取整个 AVI
缓存几十帧 raw video
同时保存大量 decoded frames
```

推荐：

```text
JPEG compressed frame
        ↓
decode buffer
        ↓
H264 input
        ↓
encode
        ↓
立刻释放 / reuse buffer
```

尽可能复用固定 buffer。

例如：

```text
jpegBuffer
rawFrameBuffer
h264OutputBuffer
muxerBuffer
```

避免循环中：

```cpp
malloc()
free()
malloc()
free()
```

优先初始化时一次分配。

---

# 48. PSRAM

如果检测到 PSRAM：

```text
优先把大 buffer 放入 PSRAM
```

例如：

```text
raw frame
JPEG buffer
H264 work buffer
```

内部 SRAM 优先保留给：

```text
Wi-Fi
BLE
FreeRTOS
Camera driver
interrupt
DMA
```

启动时记录：

```text
[MEM] Internal Heap Free
[MEM] PSRAM Free
```

开始转码前再次检查。

---

# 49. Idle 判定

只有满足以下条件才允许开始后台转码：

```text
Camera NOT recording
Camera NOT taking photo
Web live stream NOT active
No OTA
No file download
No critical BLE operation
No Wi-Fi reconfiguration
SD mounted
Free heap above threshold
PSRAM above threshold
```

建议：

```text
Idle >= 30 seconds
```

以后做成配置：

```text
transcode_idle_delay_sec
```

默认：

```text
30
```

---

# 50. 转码任务

创建独立 FreeRTOS Task：

```text
TranscodeTask
```

不要在：

```text
loop()
BLE callback
Web callback
Camera callback
```

里直接执行整个转码。

推荐：

```cpp
xTaskCreatePinnedToCore(...)
```

但 Codex 必须先检查当前：

```text
Camera task
Wi-Fi task
Web server
existing task pinning
```

再决定是否固定 CPU Core。

不要盲目 pin。

---

# 51. Task Priority

TranscodeTask 必须是低优先级。

例如：

```text
Camera / critical tasks
priority higher

TranscodeTask
priority low
```

转码属于：

```text
best effort background task
```

不能抢实时任务。

---

# 52. 可抢占设计

转码必须支持中断。

例如：

```text
用户点击 Start Recording
        ↓
set transcodeAbortRequested = true
        ↓
TranscodeTask 在 frame boundary 检查
        ↓
关闭文件
        ↓
退出
        ↓
立即释放 CPU / memory
```

必须做到：

```text
每处理一帧至少检查一次 abort flag
```

不要等完整 AVI 转完才响应。

---

# 53. 第一版暂停策略

第一版不要实现复杂的：

```text
真正 resume from exact frame
```

建议简单处理：

如果被打断：

```text
删除临时 .tmp.mp4
保留原 AVI
```

下一次 idle：

```text
从头重新转码
```

虽然效率略低，但代码可靠很多。

以后再做 checkpoint resume。

---

# 54. 文件队列

扫描录像目录，例如：

```text
/videos/
```

查找：

```text
*.avi
```

但忽略：

```text
当前正在录像的 AVI
已经存在对应 MP4 的 AVI
.tmp 文件
损坏文件
```

排序：

```text
oldest first
```

即最老录像优先转换。

---

# 55. 文件状态

推荐通过扩展名表达状态：

原始：

```text
video.avi
```

转码过程中：

```text
video.tmp.mp4
```

完成：

```text
video.mp4
```

绝对不要：

```text
直接写 video.mp4
```

因为突然断电时可能留下看起来正常但实际损坏的文件。

---

# 56. 安全完成流程

必须：

```text
video.avi
 ↓
video.tmp.mp4
 ↓
finish H264 encoder
 ↓
finish MP4 muxer
 ↓
flush file
 ↓
close file
 ↓
验证文件
 ↓
rename:
video.tmp.mp4
→
video.mp4
 ↓
确认 rename 成功
 ↓
根据策略处理 video.avi
```

---

# 57. 原 AVI 删除策略

增加配置：

```text
transcode_delete_source
```

默认建议：

```text
false
```

第一阶段不要自动删除 AVI。

测试稳定后才允许：

```text
true
```

如果开启自动删除，只有以下条件全部满足才能删除：

```text
MP4 successfully finalized
MP4 exists
MP4 file size > minimum threshold
MP4 header valid
H264 track exists
duration reasonable
no encoder error
no SD write error
```

然后：

```text
delete AVI
```

---

# 58. 更安全的删除方案

推荐增加：

```text
keep_source_hours
```

例如：

```text
24
```

流程：

```text
AVI
↓
转 MP4
↓
保留 AVI 24h
↓
确认 MP4 一直存在
↓
再删除 AVI
```

如果实现太复杂，第一版保持：

```text
Never auto-delete source
```

---

# 59. H.264 编码设置

配置项：

```text
transcode_enabled
transcode_resolution
transcode_fps
transcode_bitrate
transcode_quality
```

第一版不要暴露太多高级设置。

推荐先做：

```text
Bitrate
FPS
Enable / Disable
Delete Original
```

---

# 60. 分辨率策略

不要默认对原视频做 upscale。

例如原始：

```text
640x480
```

输出：

```text
640x480
```

如果性能不够，可以提供：

```text
Original
320x240
```

两档。

例如：

```text
Transcode Resolution:
[ Original ]
[ 320x240 ]
```

降低到 320×240 会：

```text
显著提高编码速度
显著降低文件体积
降低画质
```

---

# 61. FPS 策略

如果 AVI 是：

```text
10 fps
```

默认 H.264：

```text
10 fps
```

可允许：

```text
10 → 5 fps
```

这种模式特别适合监控录像。

例如：

```text
Source:
640x480
10 fps

Compressed:
640x480
5 fps
```

可以明显降低：

```text
CPU负担
MP4体积
```

---

# 62. 编码模式

优先：

```text
Baseline Profile
```

原因：

```text
解码兼容性最好
编码复杂度较低
适合 ESP32-S3
```

不要第一版追求：

```text
High Profile
B-frames
复杂 CABAC
高阶编码参数
```

目标首先是：

```text
稳定
```

而不是最大压缩率。

---

# 63. GOP

推荐初始：

```text
I-frame interval:
1～2 seconds
```

例如：

```text
10 fps

GOP = 10
```

或者：

```text
GOP = 20
```

监控视频可以适当增大 GOP。

但 GOP 太大会：

```text
seek 体验下降
错误恢复变差
```

---

# 64. 转码速度

必须接受：

```text
转码可能慢于实时
```

例如：

```text
源视频：
10 fps
```

ESP32 可能只能：

```text
1～5 fps
```

那么：

```text
1分钟视频
```

可能需要：

```text
数分钟
```

后台任务不需要追求实时。

重点：

```text
不影响摄像头正常使用
```

---

# 65. CPU 使用限制

如果可行，增加：

```text
transcode_cpu_throttle
```

例如每处理 N 帧：

```cpp
vTaskDelay(...)
```

或者每帧：

```text
主动 yield
```

目标：

```text
让 Wi-Fi / BLE / Web Server 保持响应
```

---

# 66. 温度保护

ESP32-S3 做软件 H.264 编码可能长时间高负载。

读取芯片内部温度。

增加：

```text
transcode_temp_limit
```

例如初始：

```text
75°C
```

不要把这个值硬编码为“硬件安全极限”。

它只是软件保护阈值。

如果：

```text
temperature > threshold
```

则：

```text
暂停转码
```

Serial：

```text
[TRANSCODE] Paused: temperature high
```

等温度下降后：

```text
继续等待下一次 idle
```

---

# 67. 电源策略

如果设备使用电池：

建议支持：

```text
Disable transcoding on battery
```

如果未来能够检测：

```text
USB power present
```

则优先：

```text
只有 USB 供电时自动转码
```

如果当前无法可靠判断电源来源：

第一版不要假设。

Web UI 提供：

```text
Background Transcoding
ON / OFF
```

---

# 68. SD 卡保护

转码意味着：

```text
同时大量 READ + WRITE
```

必须避免不必要的小块随机写。

推荐：

```text
larger sequential buffers
periodic flush
```

但不要：

```text
每一帧 fsync
```

否则 SD 性能会非常差。

最终 finalize 时必须：

```text
flush
close
```

---

# 69. 剩余空间

开始转码前检查：

```text
SD Free Space
```

至少保证：

```text
可同时容纳
原 AVI
+
临时 MP4
+
安全余量
```

推荐：

```text
estimated MP4 size
+
100 MB reserve
```

如果无法准确估计：

至少要求：

```text
free space > AVI file size
```

第一版可以采用这个保守规则。

---

# 70. 文件过大策略

不要一次处理无限大的 AVI。

增加：

```text
transcode_max_file_mb
```

例如默认：

```text
500 MB
```

超过则：

```text
skip
```

Serial：

```text
[TRANSCODE] Skip: file too large
```

以后根据实际性能调整。

---

# 71. 损坏 AVI

如果遇到：

```text
AVI header invalid
JPEG frame damaged
index missing
unexpected EOF
```

不要卡死。

标记：

```text
TRANSCODE_FAILED
```

保留 AVI。

删除：

```text
.tmp.mp4
```

继续处理下一文件。

---

# 72. Watchdog

H.264 encode / JPEG decode 可能耗时较长。

确保：

```text
Task periodically yields
```

不要用：

```text
长时间 tight loop
```

导致：

```text
Task watchdog reset
```

必要时：

```text
vTaskDelay(1)
taskYIELD()
```

但要以当前 framework 的正确做法为准。

---

# 73. Web UI

新增页面：

```text
Storage / Video Compression
```

例如：

```text
Background H.264 Compression
[ ON ]

Output:
H.264 / MP4

Idle Delay:
30 sec

Output Resolution:
Original

Output FPS:
Same as Source

Delete AVI after successful conversion:
[ OFF ]

Status:
Idle

Queue:
3 videos

Current:
VID_20261004_112315.avi

Progress:
37%

Speed:
2.4 fps
```

---

# 74. Web 转码控制

按钮：

```text
Start Now
Pause
Cancel Current
```

其中：

### Start Now

只有设备当前安全时才启动。

如果：

```text
Recording
```

返回：

```text
Busy
```

---

### Pause

停止当前转码：

```text
删除 temp
保留 AVI
```

---

### Cancel Current

等同于：

```text
abort current job
delete temp
keep source
```

---

# 75. BLE 信息

BLE 增加一个只读 characteristic：

```text
Transcode Status
```

返回例如：

```text
IDLE
```

```text
TRANSCODING
```

```text
PAUSED
```

```text
ERROR
```

也可以返回：

```text
TRANSCODING 37%
```

第一版不用通过 BLE 控制转码。

只查看状态即可。

---

# 76. 转码状态结构

建议：

```cpp
enum class TranscodeState {
    DISABLED,
    IDLE,
    WAITING,
    TRANSCODING,
    ABORTING,
    COMPLETED,
    ERROR
};
```

维护：

```text
currentFile
frameIndex
totalFrames
progress
fps
lastError
```

---

# 77. 进度计算

如果 AVI index 中能得到：

```text
totalFrames
```

则：

```text
progress =
processedFrames / totalFrames
```

如果无法知道总帧数：

使用：

```text
file read position / file size
```

作为估计值。

Web UI 明确这是：

```text
approximate progress
```

---

# 78. 日志

建议：

```text
[TRANSCODE] Queue scan started
[TRANSCODE] Found 4 AVI files

[TRANSCODE] Starting:
VID_20261004_112315.avi

[TRANSCODE] Resolution: 640x480
[TRANSCODE] FPS: 10
[TRANSCODE] H264 bitrate: xxxx

[TRANSCODE] Progress: 25%
[TRANSCODE] Progress: 50%
[TRANSCODE] Progress: 75%

[TRANSCODE] Finalizing MP4
[TRANSCODE] Complete

[TRANSCODE] Source: 182 MB
[TRANSCODE] MP4: 41 MB
[TRANSCODE] Saved: 77%
```

不要每帧打印日志。

建议：

```text
每 10%
```

或者：

```text
每 30 秒
```

打印一次。

---

# 79. Statistics

记录：

```text
Original Size
MP4 Size
Compression Ratio
Encode Time
Average Encode FPS
Peak Temperature
```

例如：

```text
Source:
184.2 MB

MP4:
39.7 MB

Reduction:
78.4%

Processing:
6m 32s

Average:
2.3 fps

Peak Temp:
68.5°C
```

这样后续可以判断：

```text
ESP32 转码到底值不值得
```

---

# 80. 性能基准测试

Codex 完成功能后至少测试：

```text
320x240
5 fps

320x240
10 fps

640x480
5 fps

640x480
10 fps
```

记录：

```text
encode fps
source size
output size
CPU behavior
free heap
PSRAM
chip temperature
```

如果某个模式明显不可用：

不要隐藏。

在文档中明确标记：

```text
Not Recommended
```

---

# 81. 自动禁用策略

如果：

```text
连续出现 3 次转码失败
```

可以：

```text
停止自动转码
```

状态：

```text
ERROR
```

需要用户通过：

```text
Web UI
```

重新启用。

避免设备：

```text
无限失败
无限重新尝试
长期 100% CPU
```

---

# 82. 开机恢复

设备重启时：

扫描：

```text
*.tmp.mp4
```

这些文件认为是：

```text
unfinished transcode
```

默认：

```text
删除 *.tmp.mp4
```

原始 AVI 保留。

然后重新加入队列。

---

# 83. 当前录像保护

当前正在录制：

```text
current_video.avi
```

必须标记成：

```text
LOCKED
```

TranscodeTask 不能打开。

只有录像完成并：

```text
AVI finalized
file closed
index written
```

以后才允许进入转码队列。

---

# 84. 文件访问锁

Camera writer 和 TranscodeTask 必须有明确的 SD 文件访问策略。

不要出现：

```text
Camera 写 SD
同时 Transcoder 做大量 SD IO
```

如果测试发现 SD 卡吞吐受影响：

录像期间：

```text
完全暂停 TranscodeTask
```

即使不同文件也暂停。

这是推荐默认策略。

---

# 85. JPEG Decoder

优先使用：

```text
已有 ESP32 / Espressif optimized JPEG decoder
```

不要自己实现 JPEG decoder。

确认：

```text
JPEG output pixel format
```

是否可以直接匹配 H.264 encoder 输入。

如果不能：

只做一次必要的：

```text
pixel format conversion
```

避免：

```text
JPEG
→ RGB888
→ RGB565
→ YUV
```

这种多次无意义转换。

---

# 86. H.264 Encoder

优先评估 Espressif 官方：

```text
esp_h264
```

不要第一选择移植大型桌面编码器，例如：

```text
x264
FFmpeg full build
```

因为：

```text
RAM
Flash
CPU
```

都不合适。

Codex 必须首先确认当前：

```text
Arduino / PlatformIO
ESP-IDF version
```

与 `esp_h264` 的兼容性。

---

# 87. MP4 Muxer

优先评估 Espressif：

```text
esp_muxer
```

如果当前 framework 无法直接使用：

可以考虑：

```text
轻量 MP4 muxer
```

但禁止为了这个功能引入完整 FFmpeg。

---

# 88. 编译系统

当前项目使用 PlatformIO。

Codex 需要确认：

```text
platformio.ini
framework
board
ESP-IDF / Arduino core version
```

加入 H.264 / muxer 组件之前：

先验证：

```text
官方组件是否兼容当前构建环境
```

如果需要升级 framework：

不要直接升级。

先在：

```text
IMPLEMENTATION_SUMMARY.md
```

说明：

```text
为什么必须升级
可能破坏什么
```

---

# 89. 第一阶段 MVP

Phase 7.1 只支持：

```text
320x240
MJPEG AVI
→
H.264 MP4
```

不要一开始支持所有分辨率。

要求：

```text
一个小 AVI
成功转换
MP4 能在 PC / iPhone 播放
```

---

# 90. Phase 7.2

增加：

```text
640x480
```

并测实际性能。

如果低于：

```text
1 fps
```

或者导致：

```text
Wi-Fi unstable
BLE disconnect
camera failure
```

则默认禁用 VGA 自动转码。

---

# 91. Phase 7.3

增加：

```text
后台队列
idle detection
abort
Web status
```

---

# 92. Phase 7.4

增加：

```text
auto delete source
temperature protection
storage protection
statistics
```

---

# 93. 可选：低 FPS 模式

对于监控录像可以增加：

```text
Archive Mode
```

例如：

```text
Source:
640x480
10 fps

Archive:
640x480
3 fps H.264
```

非常适合：

```text
长时间监控
画面变化不大
主要用来事后查看
```

可以大幅降低空间占用和编码负担。

---

# 94. 可选：转码时间窗口

以后支持：

```text
Transcode Schedule
```

例如：

```text
Only between
01:00 – 06:00
```

但第一版不做。

Idle 判定已经足够。

---

# 95. 禁止事项

不要：

```text
录像时同时 H264 转码
直接覆盖源 AVI
未验证 MP4 就删除源文件
把整个 AVI 加入 RAM
在 BLE callback 中转码
在 HTTP callback 中转码
高优先级运行 TranscodeTask
默认开启自动删除
强制所有视频必须转 H264
```

---

# 96. 失败原则

任何转码错误：

```text
保留 AVI
```

这是最高原则。

即：

```text
Transcode failure
≠
Video loss
```

---

# 97. 最终工作流程

正常：

```text
Camera Recording
      ↓
MJPEG AVI
      ↓
Recording Complete
      ↓
进入 Transcode Queue
      ↓
设备进入 Idle
      ↓
读取 AVI
      ↓
JPEG Decode
      ↓
H.264 Encode
      ↓
MP4 Mux
      ↓
.tmp.mp4
      ↓
Finalize
      ↓
Verify
      ↓
video.mp4
```

如果用户开始录像：

```text
Transcoding
      ↓
Recording Request
      ↓
Abort transcode
      ↓
Delete .tmp.mp4
      ↓
Keep AVI
      ↓
Start recording immediately
```

---

# 98. 最终验收测试

## Test A

```text
录制 1 分钟 AVI
↓
停止录像
↓
等待 30 秒
↓
自动开始转码
↓
生成 MP4
↓
iPhone 能播放
↓
PC 能播放
```

---

## Test B

```text
正在转码
↓
点击 Start Recording
↓
转码迅速停止
↓
AVI 保留
↓
新录像立即开始
```

---

## Test C

```text
转码进行到 50%
↓
设备突然断电
↓
重新开机
↓
原 AVI 仍然完整
↓
删除残留 tmp
↓
之后重新转码
```

---

## Test D

```text
SD 空间不足
↓
拒绝开始转码
↓
AVI 保留
↓
设备正常工作
```

---

## Test E

```text
损坏 AVI
↓
转码失败
↓
删除 tmp
↓
保留损坏 AVI
↓
继续处理下一个文件
```

---

# 99. 最终性能报告

Codex 最终必须在：

```text
IMPLEMENTATION_SUMMARY.md
```

加入：

```text
H.264 Transcoding Results
```

至少记录：

| Source | FPS | Encode Speed | AVI Size | MP4 Size | Reduction |
|---|---:|---:|---:|---:|---:|
| 320×240 | 5 | measured | measured | measured | measured |
| 320×240 | 10 | measured | measured | measured | measured |
| 640×480 | 5 | measured | measured | measured | measured |
| 640×480 | 10 | measured | measured | measured | measured |

不要填写理论数字。

必须使用实际 XIAO ESP32-S3 Sense 测试结果。

---

# 100. 最重要的设计原则

```text
录像 > 转码
```

以及：

```text
原始 AVI 永远优先保住
```

后台 H.264 压缩只是：

```text
Idle Optimization
```

不能成为核心录像功能的依赖。

最终理想状态：

```text
正常使用
    ↓
MJPEG AVI 快速可靠录像
    ↓
设备空闲
    ↓
后台慢慢转换
    ↓
H.264 MP4
    ↓
节省 SD 空间
+
提高兼容性
```
