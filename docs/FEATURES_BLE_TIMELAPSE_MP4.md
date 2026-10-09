# BLE、定时拍照与后台 MP4 转码

此文描述截至 2026-10-09 已确认的产品行为。实际验收结果与固件版本记录在
[实施记录](../IMPLEMENTATION_SUMMARY.md)。构建成功不代表真机验收通过。

## 使用

- Web 登录后在“定时拍照”选择预设或自定义 5–604800 秒，默认 900 秒且立即首拍。
  每次开始创建一个新文件夹。停止或重启结束当前任务；重启不会续拍。
- 事件监测与定时拍照互斥。先明确停止当前模式，再开启另一项。
  定时拍照会锁定拍摄参数。手动录像仍可使用，录像覆盖的拍照周期计为跳过。
- 文件夹可分页浏览、预览和下载单张 JPEG；整组删除需确认，运行中的组不能删除。
  删除接口只接受本功能的目录和文件名，不递归删除陌生文件或子目录。
- “后台视频转换”默认开启，开关保存在 NVS。设备连续空闲 30 秒后从最老的 AVI 开始。
  新录像最高为 640×480；转换支持 320×240、640×480 原尺寸。历史录像的其他尺寸
  保留 AVI 并显示不支持。事件监测一次开启到关闭合成一个视频，AVI 右上角已显示
  各帧真实事件时间；MP4 仅转码，保留画面中的文字、分辨率和声音。
- H.264 Baseline + AAC、MP4；保留帧数和原 AVI 时间基。MP4 完成封装、关闭并通过读回校验后
  发布到原目录的同名 `.mp4`，立即删除 `.avi`，文件大小不是删除条件。
  若同名 MP4 已存在但没有可验证的发布记录，不覆盖、不删除源 AVI。
- 拍摄、直播、下载、网络修改、OTA 和关键 BLE 操作会中断转码；整个事件监测或定时拍照
  Session 都会暂停转码。中断保留 AVI、清理本任务的 `.mp4.part`，之后从头重做。
- 内存不足时暂停并保留 AVI，等待空闲后重试，不累计为连续实际失败。分块 JPEG
  解码减少像素缓存；长片时间基使用约分及必要的 64-bit 媒体时长。详情和验收边界
  见 [转码优化记录](CODEC_OPTIMIZATION.md)。长片仍受 MP4 索引内存及 SD 空间限制。

目录示例：

```text
/20261004/20261004_120000_VGA_10_5_S.mp4
/timelapse/TL_20261004_120000_89abcdef/
  session.json
  photos.index
  IMG_20261004_120000_000001.jpg
```

未同步时间时使用 `TL_unsynced_<random>` 和 `IMG_<sequence>.jpg`；metadata 的未知 Unix 时间为 0。
时间间隔由单调时钟计量，不受校时和时区跳变影响；有可信时间时按项目时区命名。
短占用最多重试 30 秒且不越过下一周期；不补拍、不移动原时间线。
连续五次真正捕获/写盘失败停止；低于已设保留空间也会停止，已有照片继续保留。

## BLE 配对与命令

广播名称 `XIAO-Camera-<MAC最后两字节>`，本板为 `XIAO-Camera-4C08`。
Service UUID：`7A100000-8C3A-4B9F-A7B1-5D23E1000000`。
以下表中的 ID 替换 Service UUID 的末两位编号，例如 ID `0D` 为 `7A10000D-...`。
一次支持一个管理连接，连接期间停止广播；最多保存三个绑定。

基本状态无需配对。所有写操作要求 Secure Connections、PIN 认证、加密、绑定和 16 字节密钥。
在已登录 Web 查看六位 PIN；更换 PIN 会清除绑定并断开 BLE。
USB 串口（115200）发送 `BLE PIN` 查询，`BLE PIN ROTATE` 轮换；启动日志不打印 PIN。
PIN 与管理员密码独立，密码特征只写，Web/状态不会读回 Wi-Fi 密码或 WG 私钥。

| ID | 内容 | 权限 / UTF-8 命令 |
|---|---|---|
| 01 | Device status | Read / Notify |
| 02 | Wi-Fi owner state | Read / Notify |
| 03 | SSID | Read / authenticated Write，1–32 字节 |
| 04 | Wi-Fi password | authenticated Write only，0–63 字节 |
| 05 | Apply Wi-Fi | authenticated Write：`APPLY` |
| 06 | LAN IP | Read / Notify |
| 07 | mDNS hostname | Read / Notify：`xiao-camera.local` |
| 08 | RSSI | Read / Notify |
| 09 | Chip temperature | Read / Notify |
| 0A | SD state | Read / Notify |
| 0B | SD free MB | Read / Notify |
| 0C | Camera state | Read / Notify |
| 0D | Photo | authenticated Write：`PHOTO` |
| 0E | Recording | authenticated Write：`START` / `STOP` |
| 0F | Reboot | authenticated Write：`REBOOT` |
| 10 | Factory reset | authenticated Write：`FACTORY_RESET_CONFIRM` |
| 11 | WG status | Read / Notify，不能修改 WG |
| 12 | Interval seconds | Read / authenticated Write：5–604800 |
| 13 | Timelapse | authenticated Write：`START` / `START_DELAY` / `STOP` |
| 14 | Last command result | Read / Notify；`ACCEPTED` 表示交给 owner，不代表磁盘完成 |
| 15 | Conversion status | Read / Notify |
| 16 | Timelapse status | Read / Notify |

SSID / Password 必须在同一认证连接中分别写入，然后 `APPLY`。暂存五分钟或断开连接即清除。
只有 `APPLY` 才把 Wi-Fi 字段提交给现有 NetworkService。实际连接目标 SSID、取得 IP 并稳定
三秒后，BLE 专用确认路径自动保存；失败回退到旧 Wi-Fi，管理员和 WG 内容保持一致。
该路径不伪造 Web 的 LAN 访问证明，Web 自己的确认流程仍需从候选 LAN 地址提交。
首次管理员尚未建立时，请先用设备热点完成 Web 首次设置，再使用 BLE 管理。

恢复出厂由网络 owner 清理 `camcfg`、`camcapture`、`camjobs`、`camble` 和旧 Wi-Fi namespace，
清理 BLE 绑定并重启到首次设置；不会格式化或删除 microSD 媒体。

## HTTP 与实现

全部接口要求现有管理员 session；POST 还要求 CSRF 和同源校验。

| 接口 | 输入 / 行为 |
|---|---|
| GET `/api/timelapse` | 当前会话、计数、错误和下次剩余秒数 |
| POST `/api/timelapse/start` | `{"intervalSeconds":900,"immediate":true}` |
| POST `/api/timelapse/stop` | 空 body，异步停止并写 metadata |
| GET `/api/timelapse/sessions?before=<cursor>` | 最多 20 组、继续游标 |
| GET `/api/timelapse/files?session=<id>&before=<cursor>` | 最多 20 张 |
| POST `/api/timelapse/delete` | `{"session":"TL_..."}`，只删除已停止组 |
| GET/POST `/api/settings/transcode` | 状态 / `{"enabled":false}` |
| GET `/api/settings/ble` | 已登录查看 PIN，响应禁止缓存 |
| POST `/api/settings/ble` | 空 body，排队轮换 PIN |
| GET `/api/media/file?path=...` | JPEG / MP4 inline，支持单段字节 Range |
| GET `/api/media/download?path=...` | 附件下载，同样支持 Range |

摄像头仍由现有 capture task 独占。定时拍照 worker 调用同一配置拍照 mailbox，
照片保存锁保护共享 JPEG 缓冲。foreground lease 先请求中断并等待 codec 释放资源，
再执行前台操作；长运行模式和异步下载继续通过状态阻止后台重启。

NVS：`camjobs/enabled`，`camble/pin`；原相机 `camcapture` 和网络 `camcfg` 格式保持。
发布恢复记录为同目录 `.mp4.journal`，包含源和输出的字节数、CRC、尺寸、时间基及样本数。
重启后只能在源身份一致、MP4 校验通过的情况下补做 AVI 删除。
启动后也会清理严格匹配本功能保留名称的 `.mp4.part`，即使转换开关关闭。
源和 MP4 都已被删除时，仅清理具有本功能正确格式标识的遗留 journal。
三个连续真实转换错误会自动关闭并持久保存开关；暂停和不支持的尺寸不计入该保护。
内部内存不足或芯片温度超过 80°C 时暂停，源 AVI 保留。

BLE 控制器及需要内部 RAM 的任务保持 SDK 分配规则；NimBLE host 的专用分配器使用
PSRAM。照片任务、SD 转码任务和录像 staging buffer 使用 PSRAM，NVS / OTA 的任务仍用
内部栈。状态接口的响应缓冲也放在 PSRAM，以保留 HTTP 发送调用的栈空间。
公网出口验证任务的 Mbed TLS 分配按任务身份定向到 PSRAM，以避免 BLE 与 TLS 同时占用
内部 RAM。该任务仍使用内部栈、证书 bundle、SNI 和普通目的地路由；其他 Mbed TLS
调用保持 SDK 分配规则，SDK 本身没有修改。
Session metadata 使用关闭后的临时文件和上一份 `session.bak`，发布成功后清理备份。
照片和 Session 使用 64 字节固定记录索引，按新增记录倒序分页，游标是不可自行解释的继续位置。
先写照片意图，再创建 JPEG；列表跳过不存在的失败拍摄记录。扫描保护触发时仍会返回向前推进的
游标，继续加载可越过大量空记录。旧目录缺少照片索引时回退到有上限的目录扫描并提示。
摄像头缓存可能包含切换前的图像，所以拍摄路径检查 JPEG SOF 的实际尺寸，不依赖驱动的宽高字段。

软件 codec 的专用 libc 分配引用被构建脚本定向到 PSRAM，保留原始依赖 archive；SDK、DMA 和
RTOS 的分配规则保持原样。转换期间摄像头 owner 暂时释放帧缓存，转换资源释放后恢复传感器、
音频和用户参数。JPEG 全范围亮度转换到编码器的有限范围，避免播放器压黑暗部。
MP4 关闭后修正固定 muxer 的 avcC profile 字节，并把视频时间表恢复为 AVI 的准确有理时间基；
AAC 时间表使用每包 1024 个样本。容器的创建/修改时间使用 UTC 和 MP4 的 1904 年纪元，
时间未知时写 0；本地时区只用于目录和文件名。之后计算完整输出 CRC，再执行结构、样本边界
及时间表读回校验。

依赖固定在忽略的 `.tools/codec-source/`，构建脚本自动准备：

- Espressif esp_h264 1.4.1，commit `afff884d9fa6674658faaf6b2003e7969be287ef`。
- Espressif esp_audio_codec 2.6.2 / esp_muxer 1.2.3，commit `a1c9747e62e9f78444570ce6dfb0c4143de98393`。
- JPEG decoder 和 NimBLE 使用当前 Arduino 3.3.12 / IDF 5.5.5 SDK 内置组件。

不修改 SDK 或双 OTA 分区。发布仍需通过 80% app slot gate。
依赖许可证保留在 [vendor/codec-licenses](../vendor/codec-licenses/)。
`xiao-appliance-codec-test` 是专用测试环境：启动时后台禁用，并将队列源限定为测试接口明确
指定的单个 AVI；不能枚举处理历史录像，不能作为最终固件交付。直接 codec 测试不删除源；
显式 `mode: queue` 才验证同名 MP4 发布与删除测试 AVI 的正式流程。
其他认证、事件或故障夹具也禁止扫描历史转码，即使保留的 NVS 开关为开启。
测试环境还可构造合法的较大 MP4、暂停在发布边界、损坏已发布的测试输出和创建中断临时文件，
以验证删除、恢复和 CRC 保护。`/api/timelapse/test-index` 仅在测试环境中向指定的已停止组
添加 4100 条不存在的照片意图，用于验证扫描保护之后仍可继续分页。所有这些测试接口均由
编译宏排除于正常固件。
