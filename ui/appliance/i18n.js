'use strict';
// Complete local catalog. No service, cookies or credentials are used for translation.
const catalog = Object.fromEntries(`
定时拍照|Interval photos|Intervallfotos
拍照间隔|Photo interval|Aufnahmeintervall
自定义|Custom|Benutzerdefiniert
自定义间隔（秒）|Custom interval (seconds)|Eigenes Intervall (Sekunden)
开始后立即拍第一张|Take first photo immediately|Erstes Foto sofort aufnehmen
开始定时拍照|Start interval photos|Intervallfotos starten
停止定时拍照|Stop interval photos|Intervallfotos stoppen
间隔为 5 秒至 7 天。每次开始创建独立文件夹；重启后停止。运行期间锁定拍摄参数。|Intervals range from 5 seconds to 7 days. Each start creates a separate folder. Restart stops the session. Capture settings are locked while running.|Intervalle von 5 Sekunden bis 7 Tagen. Jeder Start erstellt einen eigenen Ordner. Ein Neustart beendet die Sitzung. Aufnahmeeinstellungen bleiben während der Sitzung gesperrt.
事件监测与定时拍照不能同时开启，请先停止当前模式。|Motion monitoring and interval photos cannot run together. Stop the current mode first.|Bewegungsüberwachung und Intervallfotos können nicht gleichzeitig laufen. Zuerst den aktuellen Modus stoppen.
请先停止事件监测，再开始定时拍照。|Stop motion monitoring before starting interval photos.|Vor Intervallfotos zuerst die Bewegungsüberwachung stoppen.
刷新拍照文件夹|Refresh photo folders|Fotoordner aktualisieren
拍照文件夹|Photo folders|Fotoordner
删除整个拍照文件夹|Delete entire photo folder|Gesamten Fotoordner löschen
暂无拍照文件夹|No photo folders yet|Noch keine Fotoordner
删除文件夹 {name} 中的全部照片？此操作无法撤销。|Delete all photos in folder {name}? This cannot be undone.|Alle Fotos im Ordner {name} löschen? Dies kann nicht rückgängig gemacht werden.
定时拍照：{state} · 已保存 {photos} · 跳过 {skipped} · 失败 {failures} · 下次 {seconds} 秒|Interval photos: {state} · saved {photos} · skipped {skipped} · failed {failures} · next in {seconds} s|Intervallfotos: {state} · gespeichert {photos} · übersprungen {skipped} · fehlgeschlagen {failures} · nächstes Foto in {seconds} s
运行中|Running|Aktiv
已停止|Stopped|Gestoppt
跳过|Skipped|Übersprungen
后台视频转换|Background video conversion|Videokonvertierung im Hintergrund
空闲时转换 AVI 为 H.264 MP4|Convert AVI to H.264 MP4 while idle|AVI im Leerlauf in H.264 MP4 konvertieren
保留原分辨率、时长和声音。MP4 校验成功后删除 AVI，即使 MP4 更大。事件监测和定时拍照期间暂停转换。|Keep original resolution, duration and audio. Delete AVI after MP4 validation, even if MP4 is larger. Conversion pauses during motion monitoring and interval photos.|Originalauflösung, Dauer und Ton bleiben erhalten. Nach erfolgreicher MP4-Prüfung wird AVI gelöscht, auch wenn MP4 größer ist. Während Bewegungsüberwachung und Intervallfotos pausiert die Konvertierung.
转换：{state} · {frames}/{total} 帧 · 完成 {completed} · 失败 {failed}|Conversion: {state} · {frames}/{total} frames · completed {completed} · failed {failed}|Konvertierung: {state} · {frames}/{total} Bilder · fertig {completed} · fehlgeschlagen {failed}
等待空闲|Waiting for idle|Warten auf Leerlauf
已暂停|Paused|Pausiert
已关闭|Disabled|Deaktiviert
转换中|Converting|Konvertierung läuft
已完成|Completed|Abgeschlossen
原分辨率不支持|Source resolution unsupported|Originalauflösung nicht unterstützt
同名 MP4 已存在|MP4 with this name exists|MP4 mit diesem Namen vorhanden
失败|Failed|Fehlgeschlagen
蓝牙管理|Bluetooth management|Bluetooth-Verwaltung
查看配对 PIN|Show pairing PIN|Kopplungs-PIN anzeigen
更换 PIN 并清除配对|Rotate PIN and clear bonds|PIN ändern und Kopplungen löschen
使用 nRF Connect 配对后管理设备。PIN 与管理员密码独立；USB 输入 BLE PIN 可找回。|Pair in nRF Connect to manage the device. The PIN is separate from the admin password. Enter BLE PIN over USB to recover it.|In nRF Connect koppeln, um das Gerät zu verwalten. Die PIN ist unabhängig vom Admin-Passwort. Zur Wiederherstellung BLE PIN über USB eingeben.
设备 {name} · 配对 PIN：{pin}|Device {name} · pairing PIN: {pin}|Gerät {name} · Kopplungs-PIN: {pin}
更换 PIN 将断开蓝牙并清除所有配对，确定继续？|Rotating the PIN disconnects Bluetooth and clears all bonds. Continue?|Das Ändern der PIN trennt Bluetooth und löscht alle Kopplungen. Fortfahren?
PIN 更换已请求。请稍后查看新 PIN 并重新配对。|PIN rotation requested. Check the new PIN shortly and pair again.|PIN-Änderung angefordert. Gleich die neue PIN anzeigen und erneut koppeln.
操作未完成：{error}|Operation incomplete: {error}|Vorgang nicht abgeschlossen: {error}
播放 MP4|Play MP4|MP4 abspielen
请点击播放器开始播放。|Press play to start playback.|Zum Abspielen die Wiedergabetaste drücken.
抓拍灵敏度|Motion sensitivity|Bewegungsempfindlichkeit
低|Low|Niedrig
中|Medium|Mittel
高|High|Hoch
开启事件抓拍 · 480p|Enable event capture · 480p|Ereignisaufnahme starten · 480p
停止事件抓拍|Stop event capture|Ereignisaufnahme stoppen
画面变化时保存独立片段；静止不保存。监测期间保持摄像头运行，退出登录后继续监测。|Save separate clips when the scene changes; static scenes are not saved. Monitoring keeps the camera running and continues after sign-out.|Bei Bildänderungen werden separate Clips gespeichert; ruhige Szenen werden nicht gespeichert. Die Kamera bleibt zur Überwachung aktiv, auch nach dem Abmelden.
抓拍状态：{error}|Event status: {error}|Ereignisstatus: {error}
正在确认静止；空画面尾部不会保存。|Confirming a quiet scene; the quiet tail will not be saved.|Ruhe wird bestätigt; das ruhige Clipende wird nicht gespeichert.
● 正在保存事件片段 · 480p|● Saving event clip · 480p|● Ereignisclip wird gespeichert · 480p
● 正在监测画面 · 480p|● Monitoring scene · 480p|● Bildüberwachung aktiv · 480p
本次启动已保存 {count} 个事件。|{count} events saved since boot.|{count} Ereignisse seit dem Start gespeichert.
已请求开启 480p 事件监测。|480p event monitoring requested.|480p-Ereignisüberwachung angefordert.
抓拍未能开启，请先结束录像和下载，并检查麦克风与 SD 卡。|Event monitoring could not start. Stop recording and downloads, and check the microphone and SD card.|Ereignisüberwachung konnte nicht starten. Aufnahme und Downloads beenden sowie Mikrofon und SD-Karte prüfen.
正在停止监测并保存当前事件。|Stopping monitoring and saving the current event.|Überwachung wird beendet und das aktuelle Ereignis gespeichert.
选择已加载文件|Select loaded files|Geladene Dateien auswählen
选择 {name}|Select {name}|{name} auswählen
删除所选|Delete selected|Auswahl löschen
删除全部照片和录像|Delete all photos and videos|Alle Fotos und Videos löschen
已选择 {count} 个文件|{count} files selected|{count} Dateien ausgewählt
没有可删除的文件。|No files to delete.|Keine Dateien zum Löschen vorhanden.
永久删除全部日期的 {count} 张照片和录像？此操作无法撤销。|Permanently delete all {count} photos and videos across every date? This cannot be undone.|Alle {count} Fotos und Videos aller Tage endgültig löschen? Dies kann nicht rückgängig gemacht werden.
永久删除所选的 {count} 个文件？此操作无法撤销。|Permanently delete the {count} selected files? This cannot be undone.|Die {count} ausgewählten Dateien endgültig löschen? Dies kann nicht rückgängig gemacht werden.
已删除 {count} / {total} 个文件|Deleted {count} / {total} files|{count} / {total} Dateien gelöscht
已删除 {count} 个文件。|Deleted {count} files.|{count} Dateien gelöscht.
删除已停止，已删除 {count} 个文件。请刷新检查剩余文件。|Deletion stopped after {count} files. Refresh to check the remaining files.|Löschen nach {count} Dateien angehalten. Aktualisieren, um die übrigen Dateien zu prüfen.
删除未开始或未完成。请停止录像和抓拍、结束下载，并检查 SD 卡；目录扫描不完整时不会开始清空。|Deletion did not start or finish. Stop recording, event monitoring and downloads, and check the SD card. An incomplete scan prevents clearing.|Löschen wurde nicht gestartet oder abgeschlossen. Aufnahme, Ereignisüberwachung und Downloads beenden sowie SD-Karte prüfen. Bei unvollständiger Suche wird nicht geleert.
设备控制台|Device console|Gerätekonsole
账户与安全|Account and security|Konto und Sicherheit
修改管理员密码后，所有已登录设备需要重新登录。Wi-Fi 和 WireGuard 配置会保留。|Changing the administrator password signs out every device. Wi-Fi and WireGuard settings are retained.|Nach der Änderung des Administratorpassworts müssen sich alle Geräte neu anmelden. WLAN- und WireGuard-Einstellungen bleiben erhalten.
当前密码|Current password|Aktuelles Passwort
新密码|New password|Neues Passwort
确认新密码|Confirm new password|Neues Passwort bestätigen
修改密码|Change password|Passwort ändern
两次新密码需一致，且与当前密码不同。|Both new passwords must match and differ from the current password.|Die neuen Passwörter müssen übereinstimmen und sich vom aktuellen Passwort unterscheiden.
正在验证并保存新密码，请稍候。较低频率需要更久，设备保持可用。|Verifying and saving your new password. Lower clock speeds take longer; the device remains available.|Das neue Passwort wird geprüft und gespeichert. Bei niedrigerer Taktfrequenz dauert dies länger; das Gerät bleibt erreichbar.
密码修改未被接受，请检查输入或等待当前操作完成。|The password change was not accepted. Check your entries or wait for the current operation to finish.|Die Passwortänderung wurde nicht angenommen. Eingaben prüfen oder das Ende des aktuellen Vorgangs abwarten.
密码已修改，请用新密码重新登录。|Password changed. Sign in with your new password.|Passwort geändert. Mit dem neuen Passwort anmelden.
当前密码不正确。|The current password is incorrect.|Das aktuelle Passwort ist falsch.
密码修改未能确认，请重新登录检查结果。|The password change could not be confirmed. Sign in again to check the result.|Die Passwortänderung konnte nicht bestätigt werden. Erneut anmelden, um das Ergebnis zu prüfen.
等待超时，请重新登录确认密码修改结果。|The wait timed out. Sign in again to check whether your password changed.|Die Wartezeit ist abgelaufen. Erneut anmelden, um das Ergebnis der Passwortänderung zu prüfen.
语言|Language|Sprache
中文|中文|中文
首次设置|Welcome|Ersteinrichtung
让镜头接入你的网络|Connect your camera|Kamera verbinden
连接 2.4 GHz Wi-Fi，并设置管理员密码。完成后可在家庭网络中登录设备。|Connect to 2.4 GHz Wi-Fi and choose an administrator password. Then sign in on your home network.|Mit einem 2,4-GHz-WLAN verbinden und ein Administratorpasswort festlegen. Danach im Heimnetz anmelden.
Wi-Fi 名称|Wi-Fi name|WLAN-Name
Wi-Fi 密码|Wi-Fi password|WLAN-Passwort
管理员密码|Administrator password|Administratorpasswort
至少 10 个字符，最多 128 个 UTF-8 字节。Wi-Fi 与管理员密码分别保存。|At least 10 characters, up to 128 UTF-8 bytes. Wi-Fi and administrator passwords are separate.|Mindestens 10 Zeichen, maximal 128 UTF-8-Bytes. WLAN- und Administratorpasswort werden getrennt gespeichert.
保存并测试连接|Save and test connection|Speichern und Verbindung testen
连接家庭 Wi-Fi 后，打开此地址确认|Open this address on your home Wi-Fi to confirm|Im Heim-WLAN diese Adresse zur Bestätigung öffnen
确认此网络可用|Confirm network|Netzwerk bestätigen
确认需要从新 Wi-Fi 的局域网地址访问。连接失败或未及时确认时，设备会保留热点供你重新设置。|Confirm from the new Wi-Fi LAN address. If connection or confirmation fails, the setup hotspot stays available.|Über die lokale Adresse im neuen WLAN bestätigen. Bei einem Fehler bleibt der Einrichtungs-Hotspot verfügbar.
欢迎回来|Welcome back|Willkommen zurück
你的镜头，|Your camera.|Deine Kamera.
随时就绪。|Ready when you are.|Jederzeit bereit.
登录后查看实时画面，手动录制带声音的视频。|Sign in to view live video and record with sound.|Anmelden, um das Livebild zu sehen und Videos mit Ton aufzunehmen.
登录设备|Sign in|Anmelden
正在检查设备…|Checking device…|Gerät wird geprüft…
远程访问请使用 WireGuard；本地访问请使用可信网络。|Use WireGuard for remote access and a trusted network for local access.|Für Fernzugriff WireGuard und für lokalen Zugriff ein vertrauenswürdiges Netzwerk verwenden.
摄像机|Camera|Kamera
退出登录|Sign out|Abmelden
摄像预览|Camera preview|Kameravorschau
正在连接|Connecting|Verbindung wird hergestellt
● 录制中|● Recording|● Aufnahme läuft
摄像机画面|Camera image|Kamerabild
点击开启实时预览|Start the live preview|Livevorschau starten
开启预览|Start preview|Vorschau starten
关闭预览|Stop preview|Vorschau stoppen
查看单张画面|View a still|Einzelbild ansehen
拍照并保存|Take photo|Foto aufnehmen
开始录制|Start recording|Aufnahme starten
停止录制|Stop recording|Aufnahme stoppen
设备状态|Device status|Gerätestatus
芯片温度|Chip temperature|Chiptemperatur
ESP32-S3 内部传感器|ESP32-S3 internal sensor|Interner ESP32-S3-Sensor
SD 卡可用空间|SD free space|Freier SD-Speicher
正在读取|Reading|Wird ausgelesen
网络|Network|Netzwerk
本地连接|Local connection|Lokale Verbindung
运行时间|Uptime|Betriebszeit
CPU 占用率|CPU usage|CPU-Auslastung
RAM 占用率|RAM usage|RAM-Auslastung
640 × 480 · MJPEG · 16 kHz 单声道|640 × 480 · MJPEG · 16 kHz mono|640 × 480 · MJPEG · 16 kHz mono
重启设备|Restart device|Gerät neu starten
照片与录像|Photos and videos|Fotos und Videos
刷新|Refresh|Aktualisieren
拍摄日期|Capture date|Aufnahmedatum
更早日期|Earlier dates|Frühere Daten
登录后加载 SD 卡媒体。|Sign in to view SD media.|Anmelden, um SD-Dateien anzuzeigen.
加载更多|Load more|Mehr laden
照片可直接预览。AVI 包含 MJPEG 视频和 PCM 音频，请下载后用兼容播放器打开。录制期间可浏览，结束保存后再下载录像。|Photos can be previewed here. Download AVI files for playback with an MJPEG/PCM-compatible player. Download videos after recording has finished.|Fotos lassen sich hier ansehen. AVI-Dateien herunterladen und mit einem MJPEG/PCM-fähigen Player öffnen. Videos erst nach Abschluss der Aufnahme herunterladen.
画质、声音与性能|Image, sound and performance|Bild, Ton und Leistung
照片与录像使用独立分辨率。JPEG 数值越低，画质越高，文件也越大。录像结束后再修改参数。|Photos and videos have separate resolutions. A lower JPEG value improves quality and increases file size. Change settings after recording finishes.|Fotos und Videos haben getrennte Auflösungen. Ein kleinerer JPEG-Wert verbessert die Qualität und vergrößert die Datei. Einstellungen erst nach der Aufnahme ändern.
保存参数|Save settings|Einstellungen speichern
恢复推荐参数|Recommended settings|Empfohlene Einstellungen
240 MHz 是芯片官方最高频率。降低频率可能降低帧率并延长登录等待。数字增益同时放大声音与底噪；音量失真时降低增益。|240 MHz is the official chip maximum. Lower frequencies may reduce frame rate and slow sign-in. Digital gain amplifies sound and noise; reduce it if audio distorts.|240 MHz ist die offizielle Höchstfrequenz des Chips. Niedrigere Frequenzen können Bildrate und Anmeldung verlangsamen. Digitalverstärkung erhöht Ton und Rauschen; bei Verzerrungen reduzieren.
开启省电后，无预览、拍照或录像达到空闲时限会暂停摄像头与麦克风，并启用 Wi-Fi 节电。WireGuard 和管理页面保持可访问，下一次拍摄自动唤醒。|With power saving enabled, the camera and microphone pause after the idle timeout and Wi-Fi power saving starts. WireGuard and this page remain reachable; capture wakes the camera.|Im Energiesparmodus pausieren Kamera und Mikrofon nach der Leerlaufzeit; WLAN-Energiesparen wird aktiviert. WireGuard und diese Seite bleiben erreichbar. Eine Aufnahme weckt die Kamera.
WireGuard 远程访问|WireGuard remote access|WireGuard-Fernzugriff
导入单个服务器配置，先测试远程访问。全隧道可在连接成功后单独开启；更改需要局域网确认，未确认会自动回退。|Import one server peer and test remote access first. Full tunnel can then be enabled separately. Confirm changes on the LAN; unconfirmed changes roll back.|Einen Server-Peer importieren und zuerst den Fernzugriff testen. Danach lässt sich der Volltunnel einschalten. Änderungen im LAN bestätigen; sonst werden sie zurückgesetzt.
连接模式|Connection mode|Verbindungsmodus
远程访问|Remote access|Fernzugriff
IPv4 全隧道|IPv4 full tunnel|IPv4-Volltunnel
测试连接模式|Test connection mode|Verbindungsmodus testen
远程访问保留普通上网。全隧道通过服务器上网，断线时阻止公网直连，局域网仍可访问。需要 0.0.0.0/0、DNS IPv4、MTU 不超过 1420，以及服务器转发和 NAT。IPv6 不进入隧道；全隧道下阻止 IPv6 公网直连。|Remote access keeps normal Wi-Fi internet. Full tunnel uses the server and blocks direct internet when disconnected; LAN access remains available. Requires 0.0.0.0/0, IPv4 DNS, MTU up to 1420, and server forwarding/NAT. IPv6 is not tunneled; full tunnel blocks direct IPv6 internet.|Fernzugriff behält den normalen WLAN-Internetzugang. Der Volltunnel nutzt den Server und sperrt bei Ausfall den direkten Internetzugang; das LAN bleibt erreichbar. Erfordert 0.0.0.0/0, IPv4-DNS, MTU bis 1420 und Weiterleitung/NAT am Server. IPv6 wird nicht getunnelt; direkter IPv6-Internetzugang ist im Volltunnel gesperrt.
DNS IPv4（全隧道必填）|IPv4 DNS (required for full tunnel)|IPv4-DNS (für Volltunnel erforderlich)
正在验证全隧道出口|Verifying full-tunnel exit|Volltunnel-Ausgang wird geprüft
全隧道出口已验证|Full-tunnel exit verified|Volltunnel-Ausgang bestätigt
全隧道所需配置不完整，请检查默认路由、DNS 和 MTU。|Full-tunnel settings are incomplete. Check the default route, DNS and MTU.|Volltunnel-Einstellungen unvollständig. Standardroute, DNS und MTU prüfen.
公网出口保护未就绪，请取消试运行后重试。|Internet egress protection is not ready. Cancel the trial and retry.|Internetschutz nicht bereit. Test abbrechen und erneut versuchen.
服务器名称解析暂不可用，正在等待重试。|Server name resolution is unavailable. Waiting to retry.|Servername kann derzeit nicht aufgelöst werden. Neuer Versuch folgt.
出口验证未通过，正在重试。请检查服务器转发和 NAT。|Exit verification failed. Retrying; check server forwarding and NAT.|Ausgangsprüfung fehlgeschlagen. Weiterleitung und NAT am Server prüfen; neuer Versuch folgt.
公网出口 {address} · 验证于 {seconds} 秒前|Internet exit {address} · checked {seconds}s ago|Internetausgang {address} · vor {seconds}s geprüft
全隧道已选择，正在等待出口验证。|Full tunnel selected; waiting for exit verification.|Volltunnel gewählt; Ausgangsprüfung steht aus.
当前为远程访问模式，普通上网使用 Wi-Fi。|Remote access is active. Normal internet uses Wi-Fi.|Fernzugriff aktiv. Normaler Internetzugang erfolgt über WLAN.
连接模式开始试运行。出口验证通过后，请在下方确认网络。|Connection mode trial started. Confirm the network below after exit verification.|Test des Verbindungsmodus gestartet. Nach der Ausgangsprüfung das Netzwerk unten bestätigen.
模式试运行未被接受。请从局域网访问，并等待当前操作完成。|Mode trial was not accepted. Use the LAN address and wait for the current operation.|Modustest nicht angenommen. Die LAN-Adresse verwenden und den laufenden Vorgang abwarten.
导入单个服务器配置，先试运行并确认后保存。当前模式保留本地网络；配置中的 IPv4 默认路由和 IPv6 路由会被明确排除。|Import a single-peer configuration, test it and confirm before saving. Local access is preserved; IPv4 default routes and IPv6 routes are excluded in this mode.|Eine Konfiguration mit einem Peer importieren, testen und vor dem Speichern bestätigen. Lokaler Zugriff bleibt erhalten; IPv4-Standardrouten und IPv6-Routen werden in diesem Modus ausgeschlossen.
WireGuard 配置文件（可选）|Configuration file (optional)|Konfigurationsdatei (optional)
WireGuard 配置内容|WireGuard configuration|WireGuard-Konfiguration
导入并测试远程访问|Import and test|Importieren und testen
逐项填写配置|Enter configuration fields|Konfiguration einzeln eingeben
设备 IPv4 地址 / 前缀|Device IPv4 address / prefix|Geräte-IPv4-Adresse / Präfix
设备私钥|Device private key|Privater Geräteschlüssel
服务器公钥|Server public key|Öffentlicher Serverschlüssel
预共享密钥（可选）|Preshared key (optional)|Vorab geteilter Schlüssel (optional)
服务器主机名或 IPv4 地址|Server hostname or IPv4|Serverhostname oder IPv4
服务器 UDP 端口|Server UDP port|UDP-Port des Servers
AllowedIPs（IPv4 CIDR，以逗号分隔）|AllowedIPs (comma-separated IPv4 CIDRs)|AllowedIPs (IPv4-CIDRs, durch Kommas getrennt)
DNS IPv4（可选，当前远程访问模式保留原 DNS）|DNS IPv4 (optional; this mode keeps existing DNS)|DNS-IPv4 (optional; dieser Modus behält bestehendes DNS)
Keepalive 秒数（0 为关闭）|Keepalive seconds (0 disables)|Keepalive in Sekunden (0 deaktiviert)
测试这些配置|Test configuration|Konfiguration testen
重连 WireGuard|Reconnect WireGuard|WireGuard neu verbinden
测试关闭 WireGuard|Test disabling WireGuard|WireGuard-Deaktivierung testen
清除 WireGuard 配置|Clear WireGuard configuration|WireGuard-Konfiguration löschen
密钥不会回显或导出。导入后请在下方 Wi-Fi 设置中，从设备的局域网地址确认试运行；未确认会自动回退。|Keys are never displayed or exported. Confirm the trial below from the device LAN address. Unconfirmed changes roll back automatically.|Schlüssel werden weder angezeigt noch exportiert. Den Test unten über die lokale Geräteadresse bestätigen. Ohne Bestätigung wird automatisch zurückgesetzt.
Wi-Fi 设置|Wi-Fi settings|WLAN-Einstellungen
新网络会先试运行。请在连接成功后确认；失败时自动恢复当前配置。|Test the new network and confirm after connection. Failure restores the current configuration.|Das neue Netzwerk testen und nach Verbindung bestätigen. Bei Fehlern wird die aktuelle Konfiguration wiederhergestellt.
新的 2.4 GHz Wi-Fi 名称|New 2.4 GHz Wi-Fi name|Neuer 2,4-GHz-WLAN-Name
测试新网络|Test new network|Neues Netzwerk testen
在新网络地址登录后确认|Sign in at the new address to confirm|An der neuen Adresse anmelden und bestätigen
取消试运行|Cancel trial|Test abbrechen
固件更新|Firmware update|Firmware-Update
选择本项目生成的 firmware.bin，并提供配套 SHA-256。更新时摄像机暂停，完成后自动重启，请重新登录。|Choose this project's firmware.bin and its SHA-256. The camera pauses during update, then restarts. Sign in again afterward.|Die firmware.bin dieses Projekts und deren SHA-256 auswählen. Die Kamera pausiert während des Updates und startet danach neu. Anschließend erneut anmelden.
固件文件|Firmware file|Firmware-Datei
配套校验文件（可选）|Checksum file (optional)|Prüfsummendatei (optional)
SHA-256 校验值|SHA-256 checksum|SHA-256-Prüfsumme
上传并更新|Upload and update|Hochladen und aktualisieren
固件上传进度|Firmware upload progress|Firmware-Upload-Fortschritt
小巧的设备。清晰的视野。|Small camera. A clear view.|Kleine Kamera. Klare Sicht.
录像分辨率|Video resolution|Videoauflösung
照片分辨率|Photo resolution|Fotoauflösung
录像目标帧率|Target video frame rate|Zielbildrate des Videos
录像 JPEG 品质|Video JPEG quality|JPEG-Qualität des Videos
照片 JPEG 品质|Photo JPEG quality|JPEG-Qualität der Fotos
亮度|Brightness|Helligkeit
对比度|Contrast|Kontrast
饱和度|Saturation|Sättigung
锐度|Sharpness|Schärfe
降噪|Noise reduction|Rauschminderung
色彩效果|Color effect|Farbeffekt
自动白平衡|Automatic white balance|Automatischer Weißabgleich
白平衡增益|White balance gain|Weißabgleich-Verstärkung
白平衡模式|White balance mode|Weißabgleich-Modus
自动曝光|Automatic exposure|Automatische Belichtung
曝光补偿|Exposure compensation|Belichtungskorrektur
手动曝光|Manual exposure|Manuelle Belichtung
自动传感器增益|Automatic sensor gain|Automatische Sensorverstärkung
手动传感器增益|Manual sensor gain|Manuelle Sensorverstärkung
传感器增益上限|Sensor gain ceiling|Maximale Sensorverstärkung
水平镜像|Horizontal mirror|Horizontal spiegeln
垂直翻转|Vertical flip|Vertikal drehen
坏点校正|Bad pixel correction|Korrektur defekter Pixel
亮点校正|White pixel correction|Weißpixelkorrektur
伽马校正|Gamma correction|Gammakorrektur
镜头校正|Lens correction|Objektivkorrektur
麦克风增益|Microphone gain|Mikrofonverstärkung
CPU 频率|CPU frequency|CPU-Frequenz
空闲省电|Idle power saving|Energiesparen im Leerlauf
空闲等待秒数|Idle timeout (seconds)|Leerlaufzeit (Sekunden)
保留 SD 空间（MB）|SD space reserve (MB)|SD-Speicherreserve (MB)
自然|Natural|Natürlich
负片|Negative|Negativ
黑白|Monochrome|Schwarzweiß
红色|Red|Rot
绿色|Green|Grün
蓝色|Blue|Blau
棕褐色|Sepia|Sepia
自动|Automatic|Automatisch
晴天|Sunny|Sonnig
阴天|Cloudy|Bewölkt
办公室|Office lighting|Bürobeleuchtung
家用灯光|Home lighting|Wohnraumbeleuchtung
关闭|Off|Aus
关闭（仅预览与拍照）|Off (preview and photos only)|Aus (nur Vorschau und Fotos)
开启|On|Ein
优秀|Excellent|Ausgezeichnet
良好|Good|Gut
一般|Fair|Mittel
较差|Poor|Schwach
很差|Very poor|Sehr schwach
参数存储状态：{error}|Settings storage: {error}|Einstellungsspeicher: {error}
参数会保存在设备中，重启后继续使用。|Settings persist on the device across restarts.|Einstellungen bleiben nach einem Neustart gespeichert.
暂时无法读取摄像参数。|Camera settings are unavailable.|Kameraeinstellungen sind derzeit nicht verfügbar.
推荐参数已填入，点击保存后生效。|Recommended values are filled in. Save to apply.|Empfohlene Werte sind eingetragen. Zum Anwenden speichern.
参数已保存。下一次拍摄将使用新设置。|Settings saved for the next capture.|Einstellungen für die nächste Aufnahme gespeichert.
保存未完成，请先结束录像或等待设备就绪。|Save failed. Finish recording or wait for the device.|Speichern fehlgeschlagen. Aufnahme beenden oder auf das Gerät warten.
正在采样|Sampling|Wird gemessen
内部堆 {used} / {total} · PSRAM {percent}%|Internal heap {used} / {total} · PSRAM {percent}%|Interner Heap {used} / {total} · PSRAM {percent}%
● 待机省电，拍摄时自动唤醒|● Standby · capture to wake|● Bereitschaft · Aufnahme weckt die Kamera
请输入管理员密码。|Enter the administrator password.|Administratorpasswort eingeben.
登录已过期，请重新登录。|Session expired. Sign in again.|Sitzung abgelaufen. Erneut anmelden.
● 摄像机就绪|● Camera ready|● Kamera bereit
摄像机暂不可用|Camera unavailable|Kamera nicht verfügbar
未就绪|Not ready|Nicht bereit
检查卡是否插好|Check that the card is inserted|Sitz der Speicherkarte prüfen
设备热点|Device hotspot|Geräte-Hotspot
16 kHz 单声道|16 kHz mono|16 kHz mono
麦克风已关闭|Microphone off|Mikrofon aus
固件更新中，摄像机已暂停。|Firmware update in progress. Camera paused.|Firmware-Update läuft. Kamera pausiert.
摄像机启动未完成。可以安全重启设备后再试。|Camera startup failed. Restart the device and try again.|Kamerastart fehlgeschlagen. Gerät neu starten und erneut versuchen.
设备连接中断，正在重试。|Connection lost. Retrying.|Verbindung unterbrochen. Erneuter Versuch.
正在验证密码，请稍候。240 MHz 时约需 35–50 秒；较低频率需要更久，设备保持可用。|Verifying password. About 35–50 seconds at 240 MHz; lower clocks take longer. The device stays available.|Passwort wird geprüft. Etwa 35–50 Sekunden bei 240 MHz; niedrigere Taktraten dauern länger. Das Gerät bleibt verfügbar.
设备正在处理另一项登录请求，请稍后重试。|Another sign-in is in progress. Try again shortly.|Eine andere Anmeldung läuft. Später erneut versuchen.
设备首次设置尚未完成。|Complete device setup first.|Zuerst die Ersteinrichtung abschließen.
登录请求未被接受，请重试。|Sign-in request rejected. Try again.|Anmeldung nicht angenommen. Erneut versuchen.
密码不正确，或登录请求已失效。|Incorrect password or expired sign-in request.|Falsches Passwort oder abgelaufene Anmeldeanfrage.
验证等待超时，请稍后重试。|Verification timed out. Try again shortly.|Prüfung dauert zu lange. Später erneut versuchen.
无法连接设备，请重试。|Unable to connect. Try again.|Verbindung nicht möglich. Erneut versuchen.
设置请求已失效。若账户尚未建立，请连接设备热点后刷新重试。|Setup request expired. Connect to the hotspot and reload if setup is incomplete.|Einrichtungsanfrage abgelaufen. Bei unvollständiger Einrichtung mit dem Hotspot verbinden und neu laden.
首次设置已完成，请使用你选定的管理员密码登录。|Setup complete. Sign in with your chosen password.|Einrichtung abgeschlossen. Mit dem gewählten Passwort anmelden.
连接未完成，候选配置未启用。请检查 Wi-Fi 后重新设置。|Connection failed; the candidate was not activated. Check Wi-Fi and retry.|Verbindung fehlgeschlagen; neue Konfiguration nicht aktiviert. WLAN prüfen und erneut versuchen.
Wi-Fi 已连接。请在 90 秒试运行期限内，从新的局域网地址确认。|Wi-Fi connected. Confirm from the new LAN address within the 90-second trial.|WLAN verbunden. Innerhalb des 90-Sekunden-Tests über die neue lokale Adresse bestätigen.
正在建立密码校验值，约需 35–50 秒。设备保持响应。|Creating the password verifier, about 35–50 seconds. The device stays responsive.|Passwortprüfwert wird erstellt, etwa 35–50 Sekunden. Das Gerät bleibt ansprechbar.
正在连接 Wi-Fi，热点会继续保留。|Connecting to Wi-Fi. The hotspot stays available.|WLAN-Verbindung wird hergestellt. Der Hotspot bleibt verfügbar.
连接中断，请检查网络。|Connection lost. Check your network.|Verbindung unterbrochen. Netzwerk prüfen.
管理员密码需至少 10 个字符，最多 128 个 UTF-8 字节。|Use at least 10 characters, up to 128 UTF-8 bytes.|Mindestens 10 Zeichen, maximal 128 UTF-8-Bytes verwenden.
设置未被接受，请检查输入或等待当前试运行结束。|Setup rejected. Check the fields or wait for the current trial.|Einrichtung nicht angenommen. Eingaben prüfen oder laufenden Test abwarten.
确认未被接受。请从新 Wi-Fi 的局域网地址打开此页面。|Confirmation rejected. Open this page at the new Wi-Fi LAN address.|Bestätigung nicht angenommen. Seite über die lokale Adresse im neuen WLAN öffnen.
正在确认并保存配置。|Confirming and saving.|Bestätigung und Speicherung laufen.
当前网络已确认。|Current network confirmed.|Aktuelles Netzwerk bestätigt.
新网络等待局域网确认，逾期自动回退。|New network awaits LAN confirmation; timeout restores the previous one.|Neues Netzwerk wartet auf lokale Bestätigung; bei Zeitüberschreitung wird zurückgesetzt.
正在测试新网络。|Testing the new network.|Neues Netzwerk wird getestet.
新网络连接失败，已恢复之前的配置。|New connection failed. Previous settings restored.|Neue Verbindung fehlgeschlagen. Alte Einstellungen wiederhergestellt.
设备正在连接或恢复网络。|Connecting or recovering the network.|Netzwerk wird verbunden oder wiederhergestellt.
网络连接中断，正在等待设备恢复。|Network interrupted. Waiting for recovery.|Netzwerk unterbrochen. Wiederherstellung wird abgewartet.
新网络试运行未被接受，请检查输入或等待当前操作完成。|Network trial rejected. Check the fields or wait for the current operation.|Netzwerktest nicht angenommen. Eingaben prüfen oder laufenden Vorgang abwarten.
新网络开始试运行。请连接成功后从新的局域网地址确认。|Trial started. Confirm from the new LAN address after connecting.|Test gestartet. Nach Verbindung über die neue lokale Adresse bestätigen.
网络确认未成功，请检查访问地址。|Network confirmation failed. Check the address.|Netzwerkbestätigung fehlgeschlagen. Adresse prüfen.
取消操作未确认。|Cancellation not confirmed.|Abbruch nicht bestätigt.
已关闭|Disabled|Deaktiviert
等待 Wi-Fi|Waiting for Wi-Fi|Warten auf WLAN
等待时间同步|Waiting for time sync|Warten auf Zeitsynchronisation
正在解析服务器|Resolving server|Serveradresse wird aufgelöst
正在启动|Starting|Wird gestartet
正在建立会话|Establishing session|Sitzung wird aufgebaut
远程访问会话已建立|Remote access session ready|Fernzugriff bereit
等待重试|Waiting to retry|Warten auf erneuten Versuch
启动失败|Startup failed|Start fehlgeschlagen
；导入错误：|; import error: |; Importfehler:
暂时无法读取 WireGuard 状态。|WireGuard status unavailable.|WireGuard-Status nicht verfügbar.
配置文件须为不超过 4 KB 的单个 peer。|Use one peer and a file no larger than 4 KB.|Ein Peer, maximal 4 KB pro Datei.
无法读取所选文件。|Cannot read the selected file.|Ausgewählte Datei kann nicht gelesen werden.
每个字段请填写单行内容。|Use a single line in each field.|Jedes Feld einzeilig ausfüllen.
配置已接收，正在试运行。会话成功后请从局域网确认。|Configuration received. Trial started; confirm from LAN after connection.|Konfiguration erhalten. Test läuft; nach Verbindung im lokalen Netz bestätigen.
导入未完成，请检查配置或等待当前操作结束。|Import failed. Check the configuration or wait for the current operation.|Import fehlgeschlagen. Konfiguration prüfen oder laufenden Vorgang abwarten.
正在重新建立远程连接。|Reconnecting remote access.|Fernzugriff wird neu verbunden.
重连操作未被接受，请等待当前网络操作完成。|Reconnect rejected. Wait for the current network operation.|Neuverbindung nicht angenommen. Laufenden Netzwerkvorgang abwarten.
清除已保存的 WireGuard 配置？请保留你的原始配置文件。此操作先试运行，局域网确认后才保存。|Clear the saved WireGuard configuration? Keep your original file. This starts a trial; LAN confirmation is required to save.|Gespeicherte WireGuard-Konfiguration löschen? Originaldatei behalten. Zunächst wird getestet; Speichern erfordert eine lokale Bestätigung.
清除配置的试运行已开始，请从局域网确认。|Clear trial started. Confirm from LAN.|Löschtest gestartet. Im lokalen Netz bestätigen.
清除试运行未被接受。|Clear trial rejected.|Löschtest nicht angenommen.
测试关闭 WireGuard？确认前，逾期会恢复当前配置。|Test disabling WireGuard? Without confirmation, timeout restores the current configuration.|WireGuard-Deaktivierung testen? Ohne Bestätigung wird bei Zeitüberschreitung zurückgesetzt.
关闭试运行未被接受。|Disable trial rejected.|Deaktivierungstest nicht angenommen.
已退出登录。|Signed out.|Abgemeldet.
未能确认退出，请恢复连接后重试。|Sign-out not confirmed. Restore connection and retry.|Abmeldung nicht bestätigt. Verbindung wiederherstellen und erneut versuchen.
预览连接已关闭。请检查设备状态后重试。|Preview closed. Check device status and retry.|Vorschau geschlossen. Gerätestatus prüfen und erneut versuchen.
请重新登录。|Sign in again.|Erneut anmelden.
当前单张画面已显示。|Still image displayed.|Einzelbild angezeigt.
暂时无法获取画面，请重试。|Image unavailable. Try again.|Bild nicht verfügbar. Erneut versuchen.
照片已保存到 SD 卡。|Photo saved to SD.|Foto auf SD gespeichert.
照片已保存；设备时间尚未同步，文件日期可能不准确。|Photo saved. Device time is not synchronized; the date may be incorrect.|Foto gespeichert. Gerätezeit nicht synchronisiert; das Datum kann falsch sein.
照片未能保存，请检查摄像机和 SD 卡。|Photo failed. Check camera and SD card.|Foto fehlgeschlagen. Kamera und SD-Karte prüfen.
目录较多，本次扫描已达到上限。|Directory scan limit reached.|Grenze der Verzeichnissuche erreicht.
正在加载媒体。|Loading media.|Dateien werden geladen.
还没有保存的照片或录像。|No saved photos or videos yet.|Noch keine Fotos oder Videos gespeichert.
暂时无法读取 SD 卡媒体。|SD media unavailable.|SD-Dateien nicht verfügbar.
照片|Photo|Foto
带声音的录像|Video with sound|Video mit Ton
预览|Preview|Vorschau
下载|Download|Herunterladen
改名|Rename|Umbenennen
新名称（无需扩展名，保留拍摄日期前缀；最多 64 个 UTF-8 字节）|New name (no extension; date prefix is preserved; up to 64 UTF-8 bytes)|Neuer Name (ohne Endung; Datumspräfix bleibt erhalten; maximal 64 UTF-8-Bytes)
文件已改名。|File renamed.|Datei umbenannt.
改名未完成。请使用文字、数字、空格、下划线或连字符，并先结束录像或下载。|Rename failed. Use letters, numbers, spaces, underscores or hyphens. Finish recording or downloading first.|Umbenennen fehlgeschlagen. Buchstaben, Zahlen, Leerzeichen, Unterstriche oder Bindestriche verwenden. Aufnahme oder Download zuerst beenden.
删除|Delete|Löschen
文件已删除。|File deleted.|Datei gelöscht.
删除未完成。请先结束录像或下载后再试。|Delete failed. Finish recording or downloading first.|Löschen fehlgeschlagen. Aufnahme oder Download zuerst beenden.
本次扫描达到上限，部分文件可能尚未列出。|Scan limit reached; some files may be omitted.|Suchgrenze erreicht; einige Dateien fehlen möglicherweise.
已加载 SD 卡媒体。|SD media loaded.|SD-Dateien geladen.
这一天还没有媒体文件。|No media on this date.|Keine Dateien für dieses Datum.
暂时无法加载文件。|Files unavailable.|Dateien nicht verfügbar.
登录已失效，请重新登录。|Session invalid. Sign in again.|Sitzung ungültig. Erneut anmelden.
操作未能确认，请检查设备状态后重试。|Action not confirmed. Check device status and retry.|Aktion nicht bestätigt. Gerätestatus prüfen und erneut versuchen.
正在开始录制，视频和声音将保存到 SD 卡。|Starting recording. Video and sound will be saved to SD.|Aufnahme wird gestartet. Bild und Ton werden auf SD gespeichert.
正在结束录制并保存，请稍候。|Finishing and saving recording. Please wait.|Aufnahme wird abgeschlossen und gespeichert. Bitte warten.
确认重启设备？连接会短暂中断。|Restart the device? Connection will briefly drop.|Gerät neu starten? Die Verbindung wird kurz unterbrochen.
设备正在重启，请稍后刷新页面。|Device restarting. Reload shortly.|Gerät startet neu. Seite später neu laden.
请选择配套的小型校验文件。|Choose the matching small checksum file.|Passende kleine Prüfsummendatei auswählen.
校验文件格式无效。|Invalid checksum file.|Ungültige Prüfsummendatei.
已读取 SHA-256 校验值。|SHA-256 loaded.|SHA-256 eingelesen.
无法读取更新状态。|Update status unavailable.|Update-Status nicht verfügbar.
固件大小超出设备允许范围。|Firmware size exceeds device limits.|Firmware-Größe außerhalb der Gerätegrenzen.
设备已经在更新，请等待完成。|Update already in progress. Please wait.|Update läuft bereits. Bitte warten.
确认安装所选固件？摄像机将暂停，完成后设备重启。|Install this firmware? Camera will pause and the device will restart.|Diese Firmware installieren? Kamera pausiert und das Gerät startet neu.
正在上传固件，请保持连接。|Uploading firmware. Keep the connection open.|Firmware wird hochgeladen. Verbindung aufrechterhalten.
上传完成，正在校验并准备重启。|Upload complete. Verifying and preparing restart.|Upload abgeschlossen. Prüfung und Neustart werden vorbereitet.
固件更新成功，设备正在重启。稍后请重新登录。|Firmware updated. Device restarting; sign in again afterward.|Firmware aktualisiert. Gerät startet neu; anschließend erneut anmelden.
更新未通过校验，设备将重启回当前固件。稍后请重新登录。|Update verification failed. Restarting the current firmware; sign in again afterward.|Update-Prüfung fehlgeschlagen. Aktuelle Firmware startet neu; anschließend erneut anmelden.
更新未完成：|Update failed: |Update fehlgeschlagen:
连接中断。请等待设备恢复并检查状态后重试。|Connection interrupted. Wait for recovery and check status before retrying.|Verbindung unterbrochen. Wiederherstellung abwarten und Status vor erneutem Versuch prüfen.
请输入 Wi-Fi 和管理员密码。|Enter Wi-Fi details and an administrator password.|WLAN-Daten und Administratorpasswort eingeben.
请连接设备热点后完成首次设置；若存储故障，请检查设备。|Connect to the hotspot to finish setup. Check the device if storage has failed.|Mit dem Hotspot verbinden und Einrichtung abschließen. Bei Speicherfehlern das Gerät prüfen.
无法连接设备，请检查网络后刷新。|Cannot connect. Check the network and reload.|Keine Verbindung. Netzwerk prüfen und Seite neu laden.
{hours} 小时 {minutes} 分|{hours} h {minutes} min|{hours} Std. {minutes} Min.
{minutes} 分 {seconds} 秒|{minutes} min {seconds} s|{minutes} Min. {seconds} Sek.
总容量 {size}|Total {size}|Gesamt {size}
可用内存 {heap} · PSRAM {psram}|Free heap {heap} · PSRAM {psram}|Freier Heap {heap} · PSRAM {psram}
尝试过于频繁，请 {seconds} 秒后重试。|Too many attempts. Retry in {seconds} seconds.|Zu viele Versuche. In {seconds} Sekunden erneut versuchen.
切回家庭 Wi-Fi，再打开 http://{address}/ 确认|Switch to home Wi-Fi and open http://{address}/ to confirm|Ins Heim-WLAN wechseln und http://{address}/ zur Bestätigung öffnen
切回新的 Wi-Fi，再登录 http://{address}/ 确认|Switch to the new Wi-Fi and sign in at http://{address}/ to confirm|Ins neue WLAN wechseln und auf http://{address}/ zur Bestätigung anmelden
设备地址 {address} · 已发送 {sent} · 已接收 {received}|Device {address} · Sent {sent} · Received {received}|Gerät {address} · Gesendet {sent} · Empfangen {received}
永久删除 {name}？|Permanently delete {name}?|{name} endgültig löschen?
正在上传：{percent}%|Uploading: {percent}%|Upload: {percent}%
`.trim().split('\n').map(line => { const [source,en,de]=line.split('|'); return [source,{en,de}]; }));
let language='cn';
const translatedText=new Map(),bindings=new WeakMap();
try { const saved=localStorage.getItem('camera-language');if(['cn','en','de'].includes(saved))language=saved; }
catch (_) { /* Language still works if storage is disabled. */ }
function t(source,values={}) {
  const result=language==='cn'?source:catalog[source]?.[language]||source;
  const text=result.replace(/\{(\w+)\}/g,(match,name)=>Object.hasOwn(values,name)?String(values[name]):match);
  if(translatedText.size>500)translatedText.clear();
  translatedText.set(text,{source,values});return text;
}
function translationSource(text){return translatedText.get(text)?.source||text;}
function bindTranslation(node,text){bindings.set(node,translatedText.get(text)||{source:text,values:{}});node.dataset.translatedMessage='1';node.textContent=text;}
function clearPrivateTranslation(node){bindings.delete(node);delete node.dataset.translatedMessage;node.textContent='';translatedText.clear();}
function translateInterface() {
  document.documentElement.lang={cn:'zh-CN',en:'en',de:'de'}[language];
  for(const node of document.querySelectorAll('[data-i18n]'))node.textContent=t(node.dataset.i18n);
  for(const node of document.querySelectorAll('[data-translated-message]')) {
    const binding=bindings.get(node);if(binding)node.textContent=t(binding.source,binding.values);
  }
  for(const node of document.querySelectorAll('[aria-label],[alt],[title]')) {
    for(const attribute of ['aria-label','alt','title'])if(node.hasAttribute(attribute)) {
      const key='i18n'+attribute.replace(/-([a-z])/g,(_,letter)=>letter.toUpperCase());
      if(!node.dataset[key])node.dataset[key]=node.getAttribute(attribute);
      node.setAttribute(attribute,t(node.dataset[key]));
    }
  }
  document.getElementById('language').value=language;
}
document.getElementById('language').addEventListener('change',event=>{
  language=event.target.value;
  try {localStorage.setItem('camera-language',language);}catch(_){}
  translateInterface();document.dispatchEvent(new Event('cameralanguagechange'));
});
translateInterface();
