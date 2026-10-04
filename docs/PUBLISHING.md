# 源码发布

仓库：[wanzhikid123/xiao-camguard](https://github.com/wanzhikid123/xiao-camguard)。
发布分支为 `main`；保留 `upstream` 指向原 ESP32-CAM_MJPEG2SD 项目及其许可证。
发布源码、构建脚本、功能说明和验收摘要，私密配置与本机测试产物不进入仓库。

首次发布从经过审计的当前源码快照开始，不导入上游旧历史：历史格式扫描在旧版
`certificates.cpp` 中发现完整服务器私钥。当前快照不含这些密钥；原本地
`feature/xiao-wg-ui` 开发历史保留，不推送该分支或上游标签。

`.gitignore` 排除 `.tools/`、`.pio/`、虚拟环境、缓存、日志、设备配置、密码、密钥、
环境变量文件、固件/flash/NVS 镜像及录像、录音和测试报告目录。公开示例配置继续保留。
上游 `data/` 是网页资源，`extras/` 是文档图片；这两个目录不应作为私人拍摄目录使用。

提交前检查完整暂存快照与待推送历史，不只扫描新改的文件：

```powershell
.tools/platformio/penv/Scripts/python.exe -X utf8 scripts/audit_private.py --index --history
git diff --cached --check
.tools/platformio/penv/Scripts/python.exe -X utf8 -m unittest discover -s tests -v
node --check ui/appliance/app.js
node --check ui/appliance/i18n.js
```

审计脚本仅在内存中读取本机忽略的密码/WG 配置，用其值检查暂存文件和全部可达历史对象，
包括旧文件与提交消息；不打印匹配内容、PIN、密码、密钥或 cookie。另检查凭据格式与
禁止发布的文件路径。忽略规则不会删除已经跟踪的文件，因此仍须检查完整索引。

正常固件 `xiao-appliance` 的构建和实机证据见
[实施记录](../IMPLEMENTATION_SUMMARY.md)。源码发布不等于发布测试夹具或 GitHub Release。
本轮核心功能和五分钟测试通过，六小时测试按用户要求跳过；手机及物理故障验收边界保留。
推送后验证本地 `HEAD` 与远端 `refs/heads/main` 相同，不强制覆盖远端历史。
