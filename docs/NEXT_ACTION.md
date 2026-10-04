# 后续工作

BLE 管理、后台 MP4 转换、定时拍照代码已实现，当前实机验收结果以
[实施记录](../IMPLEMENTATION_SUMMARY.md) 为准，操作说明见
[三项功能指南](FEATURES_BLE_TIMELAPSE_MP4.md)。下面的事件固件数据是此前的验收记录。

当前发布构建 `xiao-appliance` 为 2,592,848 字节 / OTA 槽 77.58%，静态 RAM 92,272。
归档 `.tools/hardware/appliance-features-final.bin`，SHA-256：
`1e5932906b354fb09896034c7ee7fba1eaa009e20b01a30b21c6685e9ab649ed`。
三项功能按用户确认执行：合格 MP4 发布后删除 AVI；后台转换默认开启，可保存关闭；
定时拍照与事件监测必须先停止另一项。仅 QVGA/VGA 转码，其他原尺寸保留 AVI。
VGA 60 秒长片实测 856 秒完成并完整解码；转换是低优先级后台任务。
最终正常固件已安装，安装 18 项、开启设置与重启 17 项复核及五分钟定时拍照通过。
六小时测试按用户最新要求停止并跳过；实体 iPhone Safari/nRF Connect、主观声音和
破坏性故障检查仍未完成。当前验收不要求等待十分钟以上的测试。

以下是此前事件版本的历史验收（2026-10-04），范围是 480p 独立事件片段和媒体多选/批量清空。
实现与自动验收见 [事件抓拍](EVENT_CAPTURE.md)、[测试记录](TEST_REPORT.md)
和 [浏览器检查](BROWSER_QA.md)。此前正常固件为 `xiao-appliance`，归档文件
`.tools/hardware/appliance-events-final.bin`，1,916,528 字节，OTA 槽占用 57.34%，
静态 RAM 103,644 字节。SHA-256：
`18b9fb47376bca344a0de857c89ab0227435efc692c646914315e26d3ba67682`。

局域网地址 **http://192.168.1.181/**，WireGuard 地址 **http://10.8.0.5/**。
DHCP 地址可能改变。保留管理员账户、Wi-Fi/WG 配置、网络修订 71、
IPv4 全隧道模式及 CPU 240 MHz。正常固件重启后抓拍关闭；需要手动开启。
监测期间不会进入摄像头待机，退出登录不会停止监测。

此前自动验收：事件正常固件 80 项浏览器/HTTP/媒体检查、58 项事件集成检查均通过。
三个独立事件 AVI 完整解码通过，音视频时长差 8–21 毫秒；恢复后的正常固件
无测试场景接口，启动关闭抓拍，全隧道出口证明正常。真实运动灵敏度仍待实测。
原有 89 个文件的路径与大小全部保留；清理了 44 个已确认属于失败测试的文件，
留下 3 个通过验证的事件片段，卡中目前共 92 个媒体文件。

## 仍需单独验收

- 用户目前不方便摆放镜头和移动物体：真实静止桌面、手/物体进入画面、
  缓慢移动和光影误触发的灵敏度验收留待方便时进行。内存模拟不等于这项实测。
- 从另一个 WG peer 对当前最终固件重新确认登录、照片和录像下载。
  早期用户确认和板端全隧道出口证明不能代替此次远端入站确认。
- 真实手机 Safari、主观语音/噪声、实测功耗、实际满卡/损坏卡、
  外部服务器迁移和密钥过期故障仍属于原计划的独立验收项。

三份新增计划已补入本轮确认决定，原正文保留。用户随后授权源码发布到
[wanzhikid123/xiao-camguard](https://github.com/wanzhikid123/xiao-camguard)，
发布安全检查与忽略规则见 [发布说明](PUBLISHING.md)。

## 构建与复测

```powershell
python scripts/pio.py run -e xiao-appliance
python scripts/pio.py run -e xiao-appliance-event-test
.tools/platformio/penv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_events.py --address 192.168.1.181 --firmware .tools/hardware/appliance-events-final-fixture.bin --restore .tools/hardware/appliance-events-final.bin --out .tools/hardware/events-fixture-repeat
.tools/browser/venv/Scripts/python.exe -X utf8 experiments/hardware/test_appliance_event_media_browser.py --address 192.168.1.181 --firmware .tools/hardware/appliance-events-final.bin --out .tools/hardware/event-media-browser-repeat
```

先分别归档相应构建的 BIN/ELF/摘要，使用新的证据目录。硬件修改测试需依次
执行。测试夹具必须恢复为正常固件；不要通过清空用户整张卡来验收按钮。
没有必要擦除 NVS 或刷写 factory 镜像。
