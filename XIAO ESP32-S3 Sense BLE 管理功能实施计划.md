> **2026-10-04 已确认实施范围**：发现、公开状态、Wi-Fi 配置/恢复、拍照/录像、定时拍照、重启与恢复出厂；独立随机六位 PIN，Secure Connections、认证加密和绑定后才允许操作。已登录 Web 与本地 USB 提供 PIN 恢复；恢复出厂保留 SD 媒体。WireGuard 仅只读状态，不开发 iOS App。具体行为及实际验收结果见 [实施记录](IMPLEMENTATION_SUMMARY.md) 和 [功能说明](docs/FEATURES_BLE_TIMELAPSE_MP4.md)。下面保留原始计划供追溯。

> **最新验收约束**：用户要求跳过需要等待十分钟以上的测试，接受几分钟检查；最新验收状态见实施记录。

# XIAO ESP32-S3 Sense BLE Management Plan

## 1. 项目目标

在现有 XIAO ESP32-S3 Sense 摄像头项目中增加 BLE 管理功能。

BLE 不替代 Wi-Fi 的视频和网页功能，而是作为：

- 设备发现通道
- Wi-Fi 配置通道
- 当前网络状态查看通道
- 当前 IP 地址查询通道
- 故障恢复通道
- 基础设备控制通道

最终用户应当能够：

1. 使用 iPhone 上的 nRF Connect 找到 `XIAO-Camera`
2. BLE 连接设备
3. 查看当前 Wi-Fi 状态
4. 查看当前 Wi-Fi SSID
5. 查看当前 IP
6. 查看 `xiao-camera.local`
7. 修改 Wi-Fi SSID / Password
8. 让设备重新连接 Wi-Fi
9. 查看芯片温度、SD 状态等基础信息
10. 通过 BLE 执行重启
11. Wi-Fi 完全失效时仍然能够通过 BLE 恢复设备

---

# 2. 总体架构

```text
                    ┌─────────────────┐
                    │     iPhone      │
                    │                 │
                    │   nRF Connect   │
                    └────────┬────────┘
                             │
                            BLE
                             │
                             ▼
                    ┌─────────────────┐
                    │ XIAO ESP32-S3   │
                    │                 │
                    │ BLE Manager     │
                    │ WiFi Manager    │
                    │ Camera          │
                    │ microSD         │
                    │ Web Server      │
                    └───────┬─────────┘
                            │
                          Wi-Fi
                            │
                            ▼
                    ┌─────────────────┐
                    │ Router / LAN    │
                    └───────┬─────────┘
                            │
                            ▼
                  http://192.168.x.x
                            or
                  http://xiao-camera.local
```

设计原则：

```text
BLE = 管理、配置、救援
Wi-Fi = Web UI、视频、照片、文件传输
```

不要尝试通过 BLE 传输实时视频。

---

# 3. BLE 设备名称

默认 BLE Device Name：

```text
XIAO-Camera
```

如果以后支持多个设备：

```text
XIAO-Camera-XXXX
```

其中 `XXXX` 可以取 MAC 地址最后两字节。

例如：

```text
XIAO-Camera-A41F
```

---

# 4. BLE GATT 设计

创建一个主要 Service：

```text
XIAO Camera Management Service
```

建议 UUID：

```text
Service UUID:
7A100000-8C3A-4B9F-A7B1-5D23E1000000
```

所有 Characteristic 使用同一 UUID namespace。

---

# 5. Characteristic 定义

## 5.1 Device Status

UUID:

```text
7A100001-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
READ
NOTIFY
```

内容：

```text
READY
BOOTING
WIFI_CONNECTING
WIFI_CONNECTED
WIFI_FAILED
RECORDING
ERROR
```

推荐采用 UTF-8 文本，第一版不要过早使用复杂 binary protocol。

---

## 5.2 Wi-Fi Status

UUID:

```text
7A100002-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
READ
NOTIFY
```

示例：

```text
CONNECTED
```

或者：

```text
DISCONNECTED
```

---

## 5.3 Wi-Fi SSID

UUID:

```text
7A100003-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
READ
WRITE
```

读取时返回：

```text
FRITZ!Box 7590
```

写入时：

```text
FRITZ!Box 7590
```

暂存在 RAM。

不要在单独写入 SSID 后立即连接。

---

## 5.4 Wi-Fi Password

UUID:

```text
7A100004-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
WRITE
```

不要允许 READ。

读取请求必须拒绝。

写入后暂存在 RAM。

不要通过 BLE 回显密码。

---

## 5.5 Apply Wi-Fi Configuration

UUID:

```text
7A100005-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
WRITE
```

写入：

```text
APPLY
```

执行：

1. 验证 SSID 不为空
2. 将 SSID / Password 写入 NVS
3. 断开当前 Wi-Fi
4. 使用新配置连接
5. 最多等待约 15 秒
6. 更新 WiFi Status
7. 更新 IP Address
8. BLE Notify 最新状态

不要在收到 Wi-Fi Password 时立即保存和连接。

---

# 6. 当前 IP 地址

这是本阶段最重要的功能。

UUID:

```text
7A100006-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
READ
NOTIFY
```

连接 Wi-Fi 成功时：

```text
192.168.178.63
```

未连接时：

```text
0.0.0.0
```

或者：

```text
NOT_CONNECTED
```

推荐使用：

```text
0.0.0.0
```

这样程序处理更加简单。

当 DHCP 地址变化时：

```text
notify()
```

手机端应立即看到更新。

---

# 7. Hostname

UUID:

```text
7A100007-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
READ
```

返回：

```text
xiao-camera.local
```

Wi-Fi Web Server 必须同时启用 mDNS。

浏览器应能够访问：

```text
http://xiao-camera.local
```

如果 mDNS 不可用，则用户通过 BLE 查看：

```text
192.168.x.x
```

---

# 8. Wi-Fi RSSI

UUID:

```text
7A100008-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
READ
NOTIFY
```

例如：

```text
-52
```

单位：

```text
dBm
```

无需每秒 Notify。

建议每 5～10 秒更新一次，或者只在 READ 时计算。

---

# 9. 芯片温度

UUID:

```text
7A100009-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
READ
```

例如：

```text
46.3
```

单位：

```text
°C
```

如果使用 ESP-IDF / Arduino 提供的内部温度 API，则需明确：

此温度主要反映芯片内部温度，而不是环境温度。

---

# 10. microSD 信息

## SD Status

UUID:

```text
7A10000A-8C3A-4B9F-A7B1-5D23E1000000
```

返回：

```text
MOUNTED
```

或：

```text
NOT_FOUND
```

或：

```text
ERROR
```

---

## SD Free Space

UUID:

```text
7A10000B-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
READ
```

单位使用 MB。

例如：

```text
13842
```

表示：

```text
13842 MB
```

---

# 11. Camera Status

UUID:

```text
7A10000C-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
READ
NOTIFY
```

可能值：

```text
IDLE
PHOTO
RECORDING
ERROR
```

---

# 12. BLE 拍照控制

UUID:

```text
7A10000D-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
WRITE
```

命令：

```text
PHOTO
```

行为：

```text
收到 PHOTO
→ 调用现有 camera capture function
→ 保存 microSD
→ 返回状态
```

不要通过 BLE 返回整张照片。

第一版只负责触发拍照。

---

# 13. BLE 录像控制

UUID:

```text
7A10000E-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
WRITE
```

支持：

```text
START
STOP
```

例如：

```text
START
```

开始录像。

```text
STOP
```

停止录像。

通过 Camera Status characteristic Notify 当前状态。

---

# 14. Device Reboot

UUID:

```text
7A10000F-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
WRITE
```

命令：

```text
REBOOT
```

收到以后：

```text
delay 500 ms
esp_restart()
```

避免收到数据后立即 restart，保证 BLE response 有时间发送。

---

# 15. Factory Reset

UUID:

```text
7A100010-8C3A-4B9F-A7B1-5D23E1000000
```

权限：

```text
WRITE
```

不要接受简单的：

```text
RESET
```

要求：

```text
FACTORY_RESET_CONFIRM
```

收到后删除：

```text
WiFi SSID
WiFi Password
WireGuard config
其他用户配置
```

但：

```text
不要删除 SD 卡照片和录像
```

之后执行：

```text
esp_restart()
```

---

# 16. WireGuard

如果当前项目已经实现 WireGuard，则新增：

## WG Status

返回：

```text
DISABLED
CONNECTING
CONNECTED
ERROR
```

---

## WG Endpoint

READ / WRITE

---

## WG Public Key

READ

---

## WG Private Key

WRITE ONLY

绝对不要允许 READ Private Key。

---

## WG Address

READ / WRITE

例如：

```text
10.0.0.5/32
```

---

## WG Apply

写入：

```text
APPLY
```

保存配置并重新初始化 WireGuard。

第一版 BLE 功能完成之后再做 WireGuard BLE 配置，不要把两个阶段混在一起。

---

# 17. NVS 数据结构

使用 Preferences / NVS。

建议 namespace：

```text
network
```

Key：

```text
wifi_ssid
wifi_pass
hostname
```

WireGuard：

```text
wg_enabled
wg_private
wg_public
wg_endpoint
wg_address
wg_dns
```

BLE 自己不要保存 Wi-Fi Password 的临时 buffer 超过必要时间。

成功保存后：

```text
clear RAM password buffer
```

---

# 18. 安全策略

第一版至少实现 BLE pairing。

不要让任何附近的人都可以：

```text
修改 Wi-Fi
重启
Factory Reset
修改 WireGuard
```

建议分成两种权限：

### 普通 READ

允许：

```text
Device Status
WiFi Status
WiFi IP
Hostname
RSSI
Temperature
SD Status
Camera Status
```

### 加密连接后 WRITE

以下必须要求 encrypted BLE connection：

```text
WiFi SSID
WiFi Password
Apply WiFi
Reboot
Factory Reset
WireGuard configuration
```

优先采用：

```text
BLE Secure Connections
```

如当前 framework 支持，则开启 bonding。

---

# 19. BLE 广播策略

Advertising packet：

```text
Device Name:
XIAO-Camera

Service UUID:
7A100000-8C3A-4B9F-A7B1-5D23E1000000
```

正常状态持续广播。

如果功耗以后成为重要问题，可以改为：

```text
开机后广播 5 分钟
```

或者：

```text
按按钮进入 BLE 配置模式
```

但第一版：

```text
BLE 始终可连接
```

方便调试。

---

# 20. BLE + Wi-Fi 并行

ESP32-S3 必须允许：

```text
BLE ON
Wi-Fi ON
```

同时运行。

代码必须避免：

```text
BLE callback
```

里面执行长时间阻塞操作。

例如不要直接在 BLE callback 内：

```text
连接 Wi-Fi 15 秒
录像
格式化 SD
长时间等待
```

BLE callback 只应该：

```text
设置 command flag
写 Queue
发 Event
```

实际操作交给主 task / FreeRTOS task。

---

# 21. 推荐软件结构

建议整理为：

```text
src/
├── main.cpp
│
├── ble/
│   ├── ble_manager.cpp
│   └── ble_manager.h
│
├── network/
│   ├── wifi_manager.cpp
│   ├── wifi_manager.h
│   ├── mdns_manager.cpp
│   └── mdns_manager.h
│
├── camera/
│   ├── camera_manager.cpp
│   └── camera_manager.h
│
├── storage/
│   ├── sd_manager.cpp
│   └── sd_manager.h
│
├── config/
│   ├── config_manager.cpp
│   └── config_manager.h
│
└── web/
    ├── web_server.cpp
    └── web_server.h
```

不要把所有 BLE callback 塞进 `main.cpp`。

---

# 22. BLE Manager API

建议：

```cpp
bool bleInit();

void bleLoop();

void bleUpdateWiFiStatus();

void bleUpdateIPAddress();

void bleUpdateCameraStatus();

void bleUpdateSDStatus();

void bleNotifyStatus();
```

不要让 BLE Manager 自己直接管理 camera 或 SD。

通过：

```text
event
callback
queue
```

调用对应模块。

---

# 23. Wi-Fi Manager API

建议：

```cpp
bool wifiInit();

bool wifiConnect();

bool wifiConnect(
    const String& ssid,
    const String& password
);

void wifiDisconnect();

String wifiGetSSID();

String wifiGetIP();

int wifiGetRSSI();

bool wifiIsConnected();
```

---

# 24. 开机逻辑

推荐：

```text
BOOT
 │
 ├── 初始化 Serial
 │
 ├── 初始化 NVS
 │
 ├── 初始化 BLE
 │
 ├── 初始化 Camera
 │
 ├── 初始化 SD
 │
 ├── 读取 Wi-Fi 配置
 │
 ├── Wi-Fi 配置存在？
 │       │
 │       ├── YES → 尝试连接
 │       │
 │       └── NO → 保持 BLE 配置模式
 │
 ├── Wi-Fi Connected？
 │       │
 │       ├── YES
 │       │    ├── 启动 mDNS
 │       │    ├── 启动 Web Server
 │       │    └── BLE Notify IP
 │       │
 │       └── NO
 │            └── BLE 保持可访问
 │
 └── Main Loop
```

关键点：

**Wi-Fi 失败绝对不能影响 BLE。**

---

# 25. iPhone nRF Connect 使用流程

测试设备：

```text
iPhone
```

App：

```text
nRF Connect for Mobile
```

操作：

```text
1. 打开 nRF Connect
2. Scan
3. 找到 XIAO-Camera
4. Connect
5. 展开 XIAO Camera Management Service
6. 找到 WiFi Status
7. READ
8. 找到 IP Address
9. READ
```

预期：

```text
WiFi Status
CONNECTED

IP Address
192.168.178.63

Hostname
xiao-camera.local
```

然后打开 Safari：

```text
http://192.168.178.63
```

或者：

```text
http://xiao-camera.local
```

---

# 26. 第一次配置 Wi-Fi

nRF Connect：

### Step 1

写入：

```text
WiFi SSID
```

例如：

```text
FRITZ!Box 7590
```

### Step 2

写入：

```text
WiFi Password
```

### Step 3

向：

```text
Apply WiFi
```

写：

```text
APPLY
```

### Step 4

观察：

```text
WiFi Status
```

应依次变为：

```text
WIFI_CONNECTING
```

然后：

```text
CONNECTED
```

### Step 5

读取：

```text
IP Address
```

得到：

```text
192.168.xxx.xxx
```

---

# 27. Wi-Fi 配置失败

如果密码错误：

```text
WiFi Status:
FAILED
```

同时：

```text
IP Address:
0.0.0.0
```

BLE 不断开。

用户必须能够重新写入：

```text
SSID
Password
APPLY
```

不能因为错误密码陷入 reboot loop。

---

# 28. Web UI 增加 BLE 信息

现有 Web UI 增加：

```text
Bluetooth
--------------------
BLE: Enabled
Device Name: XIAO-Camera
Connected Client: Yes / No
```

如方便，可以额外显示：

```text
BLE MAC Address
```

---

# 29. 日志

Serial 输出：

```text
[BLE] Initializing
[BLE] Advertising started

[BLE] Client connected

[WiFi] Applying BLE configuration
[WiFi] Connecting to FRITZ!Box 7590

[WiFi] Connected
[WiFi] IP: 192.168.178.63

[mDNS] xiao-camera.local started

[BLE] IP characteristic updated
```

任何日志：

```text
禁止打印 Wi-Fi Password
禁止打印 WireGuard Private Key
```

---

# 30. BLE MTU

初始化 BLE 时允许进行 MTU negotiation。

不依赖巨大 MTU。

所有关键配置数据应该可以在合理长度 characteristic 中传输。

SSID 最大支持：

```text
32 bytes
```

Password 最大：

```text
64 bytes
```

如果未来 WireGuard 配置字符串过长，再实现 chunk protocol。

第一版不要提前复杂化。

---

# 31. Phase 1

第一阶段只完成：

```text
BLE advertising
BLE connect
Device Status
WiFi Status
WiFi IP
Hostname
RSSI
Temperature
SD Status
```

验收：

iPhone 可以连接并看到上述信息。

---

# 32. Phase 2

加入：

```text
WiFi SSID WRITE
WiFi Password WRITE
Apply WiFi
```

验收：

完全不通过 USB Serial，

仅使用 iPhone BLE，

可以让 ESP32-S3：

```text
从一个 Wi-Fi
切换到另一个 Wi-Fi
```

并得到新的 IP。

---

# 33. Phase 3

加入：

```text
Take Photo
Start Recording
Stop Recording
Reboot
```

---

# 34. Phase 4

加入：

```text
BLE Security
Pairing
Bonding
Encrypted writes
Factory Reset
```

如果安全实现比较容易，也可以提前到 Phase 2。

---

# 35. Phase 5

加入 WireGuard BLE 管理：

```text
WG Status
WG Endpoint
WG Address
WG Keys
WG Enable
WG Apply
```

敏感数据必须 protected write。

---

# 36. Phase 6

可选：

以后开发自己的 iOS App。

App 页面：

```text
XIAO Camera
────────────────────

● BLE Connected

Wi-Fi
Connected
FRITZ!Box 7590

IP
192.168.178.63

[ Open Camera ]

Camera
Idle

[ Take Photo ]
[ Start Recording ]

Storage
SD: 13.8 GB free

Device
Temperature: 46.3°C

[ Network Settings ]
[ Reboot ]
```

其中：

```text
Open Camera
```

自动打开：

```text
http://192.168.178.63
```

但目前不开发 App。

第一版直接使用：

```text
nRF Connect
```

---

# 37. 不要做的事情

本阶段不要：

```text
通过 BLE Stream 摄像头视频
通过 BLE 下载大型录像
自己实现复杂 BLE IP stack
BLE 直接运行完整 HTTP server
重新设计现有 Web UI
重写 Camera driver
重写 SD driver
```

BLE 只增加一个管理层。

现有正常功能不得被破坏。

---

# 38. 稳定性要求

以下场景全部必须测试：

```text
Wi-Fi 正常 + BLE 正常

Wi-Fi 失败 + BLE 正常

Wi-Fi 密码错误 + BLE 正常

Router 重启 + BLE 正常

手机 BLE 断开 + Camera 正常

手机重新 BLE Connect

SD 不存在 + BLE 正常

Camera 初始化失败 + BLE 正常

Web Server 工作时 BLE Connect

录像时 BLE Connect

BLE 修改 Wi-Fi 后重新获得 IP
```

---

# 39. 资源监控

因为：

```text
Camera
Wi-Fi
Web Server
BLE
SD
WireGuard
```

会同时使用 RAM，因此增加启动日志：

```text
Free Heap
Minimum Free Heap
PSRAM Free
```

例如：

```text
[MEM] Heap Free: xxxx
[MEM] Heap Min: xxxx
[MEM] PSRAM Free: xxxx
```

每个主要模块初始化完成后打印一次。

不要高频打印。

---

# 40. 最终验收标准

这个功能只有满足以下流程才算完成：

### Scenario A：正常运行

```text
XIAO 开机
↓
自动连接 Wi-Fi
↓
BLE 出现 XIAO-Camera
↓
iPhone nRF Connect 连接
↓
WiFi Status = CONNECTED
↓
IP = 192.168.x.x
↓
Safari 输入该 IP
↓
Camera Web UI 正常打开
```

### Scenario B：换路由器

```text
旧 Wi-Fi 不存在
↓
XIAO 无法联网
↓
BLE 仍然出现
↓
iPhone 连接 BLE
↓
写入新 SSID
↓
写入新 Password
↓
APPLY
↓
ESP32 连接新 Wi-Fi
↓
BLE 返回新 IP
↓
Safari 打开新 IP
```

### Scenario C：密码输错

```text
写入错误 Password
↓
WiFi Status = FAILED
↓
BLE 仍保持工作
↓
重新写正确 Password
↓
APPLY
↓
连接成功
```

这三个场景必须全部通过。

---

# 41. Codex 工作要求

开始修改代码之前：

1. 阅读整个 repo
2. 找出目前：
   - Wi-Fi 初始化代码
   - NVS 配置代码
   - Web Server
   - Camera
   - microSD
   - WireGuard
3. 不要先假设项目结构
4. 优先复用现有模块
5. 不要复制第二套 Wi-Fi Manager
6. 不要破坏现有 Web 配置页面
7. 每完成一个 Phase 都进行编译
8. 修复全部 compiler warning
9. 检查 ESP32-S3 RAM / PSRAM 使用情况

最终提供：

```text
IMPLEMENTATION_SUMMARY.md
```

其中记录：

```text
新增文件
修改文件
BLE UUID
NVS key
安全策略
测试方法
已知限制
```

---

# 最终设计原则

整个项目应该形成：

```text
BLE
 │
 │ 管理 / 配置 / 恢复
 │
 ▼
XIAO ESP32-S3 Sense
 │
 ├── Wi-Fi
 │    └── Web UI / Video / Download
 │
 ├── Camera
 │
 ├── microSD
 │
 ├── WireGuard
 │
 └── mDNS
      └── xiao-camera.local
```

最重要的设计目标：

> 无论 Wi-Fi 配置发生什么问题，只要设备还正常启动，用户都应该能够通过 BLE 找到设备，并恢复它的网络配置。
