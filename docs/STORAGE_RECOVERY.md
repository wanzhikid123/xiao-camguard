# SD 故障恢复

更新：2026-10-04。正式摄像固件不自动格式化、不自动删除。SD 挂载失败时，
使用固件内置页面与 NVS 中的账户/网络，仍可管理、查看单张画面和进行 OTA。
保存照片、开始录像和媒体列表会返回明确的 SD 不可用错误。

显式 `xiao-appliance-sd-absent-test` 在初始化 SD 之前返回失败，不读写插入的卡。
此 fixture 已在真实开发板通过 **24 项检查**，包括 WG 安装、缺挂载的登录、
源文件一致的内置页面/脚本/翻译/样式、拒绝 SD 操作、JPEG 完整解码、重启后
账户/摄像/WG 可用，以及通过 WG 恢复正式镜像。

恢复后所有既有媒体路径和大小、31 项参数保持一致。Fixture 已不在运行，
不要日常刷入它。此结果模拟的是缺挂载，不代替真实拔卡、接触故障、损坏 FAT
或写入过程中掉卡的验证。

保留空间默认 1024 MB。暂设高于整卡容量的额度已验证拍照和开始录像返回
507，已有文件保留并恢复原设置；没有为了测试写满卡。
不把额度拒绝称为真正的满卡写失败。

本机忽略证据：`.tools/hardware/appliance-sd-absent/report.json`、`without-sd.jpg`；
fixture 1,855,136 字节，正常恢复镜像 1,882,144 字节。

2026-10-04，当前正式传感器待机镜像的插卡基线已通过 **11 项检查**：
摄像/SD/WG 正常、四份嵌入资源与源文件一致、JPEG 完整解码；保存 78 个
既有媒体路径/大小、31 项参数和网络版本 45，CPU 为 240 MHz。
该基线时没有录像或文件写入进行。它仅记录插卡起点；后来实际缺卡与插回
检查使用下述新版基线，不能把这个历史基线当作真实缺卡结果。

管理员改密/恢复和最终镜像更新后，另存新版插卡基线
`physical-card-account-final/baseline.json`，同样通过 **11 项检查**。
当前四份资源与源码一致、CPU 240 MHz，保存 79 个媒体、31 项参数和网络版本 47。
旧基线的资源/版本与当前固件不匹配，不能用于后续实际缺卡对比。
新版基线已完成登录、完整 JPEG 解码并注销；当前没有录制/文件写入。
用户随后实际断电取卡并上电。首次正常镜像的功能检查通过，但串口回放发现
**814 次** `motionDetect: image dimensions are too large`。自动录像仍关闭；
问题是手动模式仍调用旧模块的亮度采样，而缺少 SD 配置时 `colorDepth` 为零。
正式手动模式不使用运动/亮度/PIR 结果，现跳过这段帧分析；原 baseline 保留。

修正版通过缺卡状态下的认证 OTA，并保持原密码、31 项参数和网络版本 47。
独立重跑通过 **24 项缺卡检查**，含正常重启与两个完整 JPEG；三分钟串口
及快速回放检查均为 **零次**上述错误。原功能报告与失败回放保存在
`physical-card-account-final/`，新结果在 `physical-card-boot-fix/`。
新目录复制同一份公开的 revision-47 插卡基线；页面资源、账户/网络/参数
未改变，惟有此明确记录的捕获代码修复。

用户已断电插回原卡并上电。修正版恢复阶段 **28 项检查全部通过**：
全部 79 个原媒体路径/大小、31 项参数和网络版本 47 保留；正常 JPEG 完整解码，
新照片保存/完整下载/解码成功；新建带真实 PDM 音频的 AVI 也保存、完整下载并
通过 FFmpeg 全段解码。AVI 为 1,075,658 字节，视频 8.277 秒、PCM 8.288 秒，
差 11 ms。两分钟串口记录 1,398 字节，无旧帧分析错误。此阶段保持固件和网络
配置不变，新建的照片/视频保留在卡上，证据为 `restored-report.json`、
`restored-serial.log` 及下载媒体，均在 `physical-card-boot-fix/`。

真实拔卡的正式固件验收脚本按以下三个阶段执行：

```powershell
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_physical_card.py --address 192.168.1.181 --stage baseline --out .tools/hardware/physical-card-account-final
# 由用户先断开 USB 电源，取出 SD，再接回电源。
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_physical_card.py --address 192.168.1.181 --stage absent --out .tools/hardware/physical-card-boot-fix --serial-log .tools/hardware/physical-card-boot-fix/absent-serial.log
# 由用户先断开 USB 电源，插回 SD，再接回电源。
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_physical_card.py --address 192.168.1.181 --stage restored --out .tools/hardware/physical-card-boot-fix --exercise-storage --serial-log .tools/hardware/physical-card-boot-fix/restored-serial.log
```

脚本只保存公开的参数、媒体路径/大小与网络版本；不保存凭据或会话，不更新
固件、不格式化、不删除。缺卡阶段检查正式账户、内置资源、WG、受保护 SD
接口明确失败、完整 JPEG 解码及正常重启；插回后核对所有既有媒体与原参数。
恢复阶段还会新建、下载并完整解码一张照片和一段带声音 AVI，保留所有既有文件。
证据分目录保留：`physical-card/`、`physical-card-account-final/`、`physical-card-boot-fix/`。
这仍不验证真正写满/损坏卡或带电拔卡。修正版缺卡检查后至插回阶段保持固件/配置不变。
