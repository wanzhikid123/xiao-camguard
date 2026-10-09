# 事件录像与 idle H.264 修复 — 2026-10-08

后续用户调整：时间改为录制时写入 AVI，MP4 仅转码。以下构建和验收记录属于
此前的 MP4 叠字阶段；当前 AVI 行为及新验收见 [EVENT_AVI_CLOCK.md](EVENT_AVI_CLOCK.md)。

用户确认的行为是：从一次开启监测到停止监测，只生成一个事件视频，所有运动
片段拼接在一起，右上角显示实际事件日期和时间。静止间隔不占视频播放时间。

## 修改

- 手动录像设置与 HTTP API 仅允许 QVGA/VGA，最高 640×480。读取旧版已校验
  的高分辨率录像配置时只把录像尺寸降为 VGA；照片尺寸和其他参数保留。
- 运动结束时暂停写入，停止整个监测时才完成一个 AVI；没有运动不生成文件。
  事件索引写入 SD 临时文件，取消此前按时长/帧数拆分的行为。AVI 1.0 格式边界
  仍会安全停止，状态报告 `event_file_limit`，不会默默创建第二个文件。
- 每帧 JPEG 用标准 APP15 段携带采集时间，候选静止尾部也保留其原时间。
  转成 H.264 时将时间直接写入右上角像素，MP4 下载后仍可见。时间沿用设备
  时区，与文件名一致；现有设备配置为 GMT0，本次没有修改时区。
- 手动 AVI 完成后释放已落盘的索引缓冲。下一次录像按需重新分配，避免
  遗留的 PSRAM 占用阻止 idle VGA 编码器初始化。
- AVI 文件完成时使用写入位置计算 RIFF 大小；SD 写句柄在 flush/close 前的
  `size()` 可能仍是 0，不能用于填写此字段。

## 复现与根因

旧固件现场已进入 `captureIdle=true`，但转换报告 `h264_init_failed`、0 帧和
连续三次失败，保护逻辑将后台转换设置持久化为关闭。不是 idle 调度未触发。

同一份 32 帧 VGA/PCM 源文件在重启后的旧编码测试环境可以转换，排除了源
文件损坏。随后执行一次约六秒的普通录像，空闲 PSRAM 从 3,628,444 降为
3,256,432 字节；紧接着调用实际 VGA 编码器再次在 0 帧失败。
`maxFrames=20000` 的 AVI 索引分配约 320 KB，完成录像后仍被保留。
旧事件 writer 也使用同一索引分配路径，因此事件录像同样会留下此占用。
释放此缓冲后的正常固件实测录像前后 PSRAM 均为 3,627,956 字节。

新事件 writer 初次验收也发现一个封装问题：完整音视频可解码，但 RIFF 大小
为 `0xfffffff8`，导致播放器报告视频时长 0。只修正该字段后原测试文件恢复
3.731 秒视频/3.712 秒 PCM，完整解码通过；实机回归同时检查最终 RIFF 大小。

私有原始证据保存在忽略目录 `.tools/hardware/event-session-fix/`：
`idle-red.json`、`recording-conversion-red-final.json`、`failing-event.avi`、
`normal-green.json`、`acceptance/report.json`。这些证据不包含密码、token 或密钥。

## 构建

当前正常环境 `xiao-appliance` 为 **2,595,840 字节 / OTA 槽 77.67%**，
静态 RAM 92,944 字节，低于 80% 发布预算。归档
`.tools/hardware/event-session-fix/production.bin` 和同名 ELF；SHA-256：
`c682f0e656eadd6290be5814b3d7b5031b28cc3782e434b5cc4d4416b879e9c8`。

组合夹具为 2,604,544 字节 / 77.93%，静态 RAM 93,512 字节；SHA-256：
`2f451b04cf86ff0e2298b7a42f6562941eb9164330adae18785201b7404a1756`。
最终正常构建 312.28 秒，夹具 325.31 秒，编译/链接和预算检查均通过。
夹具不用于正常使用，验收后必须恢复正常固件。
收尾检查将索引释放限定为已先写完索引的普通 AVI 路径；旧版 AVI timelapse
写头后才写索引，保留其缓冲使用顺序。两次事件转码测试使用的索引释放路径
与最终普通录像路径相同。

## 回归方式

`.tools/hardware/event-session-fix/acceptance-green/report.json` 的 **64 项实机
检查全部通过**：两段事件在停止时只产生一个文件、静止期间没有已完成文件、
空监测不生成视频、每帧时间完整、静止间隔去除后仍保留真实时间跳跃；普通录像
释放索引后，连续两次 idle VGA H.264/AAC 转换成功，中间没有重启。
两份 MP4 均保留源帧数和视频时长，完整解码及右上角实际像素检查通过。

| 监测内容 | 帧数 | AVI 视频 / PCM 秒 | MP4 视频 / AAC 秒 |
| --- | ---: | ---: | ---: |
| 两次运动合并，省略中间静止 | 52 | 3.731 / 3.776 | 3.731 / 3.776 |
| 第二次监测的一次运动 | 24 | 1.713 / 1.696 | 1.713 / 1.728 |

原有 11 个媒体路径和大小全部保留，测试文件清理完成，原拍摄参数恢复，
正常固件不存在场景/编码夹具路由。主机 9 项既有单元检查、Python/JavaScript
语法和 Git 空白检查通过。时间画面证据为该目录下
`session-0-timestamp.png`、`session-1-timestamp.png`。

最终正常固件的 **33 项发布复核全部通过**，证据为同一私有根目录的
`release.json`。安装摘要与上述 `production.bin` 一致；相机、SD、BLE 和
全隧道出口复核正常，四项页面资源与构建嵌入内容一致，夹具接口均为 404。
普通录像前后 PSRAM 为 3,627,756 / 3,627,320 字节，不再留下约 320 KB 索引。

重新开启后台转换后，正常队列连续完成三份历史录像，失败计数 0。
最初失败的 `/20261008/20261008_164432_86a63b_event_VGA_S.avi` 在 **43.578 秒**
内完成 32 帧 VGA H.264/AAC MP4，主机全文件解码通过。视频 2.297 秒，音频
2.304 秒；仅在 MP4 成功发布后删除该 AVI。测试前的 11 个文件全部仍保留原
AVI 或已成功转换为对应 MP4。后台转换保留为开启，事件监测和定时拍照停止。

发布复核首次读到公网探测失败，等待新探测后重新验证通过；页面逐字节核对
则需沿用 `scripts/embed_ui.py` 的标准换行处理，不能直接比较 Windows CRLF。
这些失败检查分别保留为 `release-network-pending.json`、`release-crlf-check.json`，
不计入最终通过项。当前记录没有把缓存出口地址单独作为网络成功的证据。

```powershell
.tools/platformio/penv/Scripts/python.exe -X utf8 scripts/pio.py run -e xiao-appliance-event-codec-test
# 先归档测试环境 BIN/ELF，再构建正常环境；不能同时构建两个环境。
.tools/platformio/penv/Scripts/python.exe -X utf8 scripts/pio.py run -e xiao-appliance
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_event_session.py --firmware .tools/hardware/event-session-fix/fixture.bin --restore .tools/hardware/event-session-fix/production.bin --out .tools/hardware/event-session-fix/acceptance-green
```

集成测试在真实 XIAO 板上使用实际 VGA JPEG、PDM 音频、SD 写入、OTA 与
idle 转换任务。场景夹具只在 JPEG 解码后替换小尺寸运动检测输入，转码任务
只允许明确指定的本轮测试文件。历史录像在此阶段禁止转换，原媒体逐项核对，
最后只删除测试登记拥有的文件并恢复普通固件和原拍摄参数。

物体实际移动的灵敏度、主观音质和长时间监测不由场景夹具证明。
转换仍为慢于实时的后台软件编码，需停止监测并等待空闲后处理。
中断/失败保留 AVI；只有 MP4 完成、验证并发布成功后才删除 AVI。
