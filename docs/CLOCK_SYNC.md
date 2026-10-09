# Wi-Fi 校时与德国时区 — 2026-10-09

Wi-Fi 联网后通过 NTP 校准 UTC 时钟。Wi-Fi 本身不提供所在地时区；本产品
使用 Europe/Berlin 的 POSIX 规则 `CET-1CEST,M3.5.0/2,M10.5.0/3`，冬季为
UTC+1，夏季为 UTC+2，并自动处理三月和十月最后一个星期日的切换。

## 原因与修正

真实设备重启后，USB 日志显示 NTP 同步成功但时区为 GMT0。
`test_appliance_clock.py` 创建的新照片与德国时间相比为 **-7200 秒**。
NTP 校时正常；两小时偏差来自 UTC 被作为当地时间使用。

正常固件的 `appliancePrepare()` 在旧配置加载之后、联网之前应用德国时区并
重新配置 NTP，同时更新 RAM 中的旧配置向量，使 `prepRecording()` 再次加载
该向量时保持一致。不重写用户 SD 配置，也不修改网络、账户或采集参数。
既有文件名和已经印在录像画面里的旧时间保留原值。

照片/手动录像文件名使用 `dateFormat()`，事件画面时间和定时照片使用
`localtime_r()`；均使用同一 C 运行时时区。MP4 继续保留 AVI 已有的时间像素。
此变更只适用于 `XIAO_APPLIANCE`，上游其他固件配置保留原行为。

已登录的 `/api/status` 提供 `timeSynchronized`、`timezone`、`localTime` 和
`epochSeconds`。UTC epoch 不人为加两小时，时区换算仅用于本地显示和命名。
SNTP 按已有 SDK 行为持续校时；不要求打开浏览器或传递浏览器时间。
全隧道的原有受限数字 NTP 引导及网络策略保持不变。

## 验证

- 红色复现：NTP 已同步，新照片实际时间偏差为 -7200 秒；测试照片已清理，
  原有媒体路径/大小保留。初次设备报告相机启动失败，重启后恢复拍照；
  此启动失败的原因未在本次时区修正中定位。
- POSIX libc 时区规则与独立 Europe/Berlin 数据库比较通过：冬季、夏季、
  春季切换前后、秋季切换前后、本地跨午夜，共七个 UTC 时刻；文件目录日期
  与显示时刻一起检查。此项由 WSL Python 运行，不是 ESP32 冬夏切换的实机模拟。

- 正常固件编译通过：2,632,288 字节 / OTA 槽 78.76%，静态 RAM 93,440 字节；
  80% 发布预算通过。SHA-256：
  `3ac53fa8febeb5fa2da3e979c8a2646388be36e70c8f8e766d57c61bd863442b`。
- 已通过 authenticated LAN OTA 安装上述正常镜像，24 项实机检查通过。真实 UTC epoch 与电脑一致，
  状态中的当地时间与 Berlin 一致；新照片偏差为 0 秒，短手动 AVI 文件名的
  当地时间正确，完整 MJPEG/PCM 解码通过。测试仅删除本次拥有的照片/AVI，
  原有媒体路径/大小保留，后台转换开关恢复。
- 独立软件重启检查通过：相机/SD 健康、31 项采集参数和网络配置 revision
  保留，正常固件的两个夹具接口均返回 404；UTC epoch、Berlin 当地时刻及
  新照片时间正确，照片偏差仍为 0 秒，原有媒体保留。未向设备发送浏览器时间。
  此项为软件重启，不是断电后时钟归零的实机校时测试。

本次新录像验证使用正常固件的手动录制，没有另录事件场景来逐帧检查时间
像素。事件画面与定时照片的时区继承关系由源码确认；旧版事件像素/转码
验证仍见 [EVENT_AVI_CLOCK.md](EVENT_AVI_CLOCK.md)。未进行长期离线时钟漂移测试。

私有红/绿/重启报告和固件归档位于忽略目录 `.tools/hardware/appliance-clock/`。
SDK 校时与时区规则依据：
[ESP-IDF 5.5.5 System Time](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/system_time.html)。
