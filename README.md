# HoloArch

这台 Arch / niri 电脑的 Clavis 桌面源码、个人适配和恢复基线。Clavis 与 key-cli 由本仓库自行构建、部署和维护。

2026-10-01 已完成本机编译、双屏图形试运行和正式自启动接入。首次备份保留在 `pre-clavis-20261001` 标签；原始归档和 root #1119 / home #915 快照仍在本机。

## 常用入口

| 操作 | 快捷键 |
| --- | --- |
| 应用菜单 | Mod+Z |
| 壁纸 | Mod+Alt+W |
| 剪贴板 | Mod+Alt+V |
| 快捷设置，含电源/GPU、系统维护 | Mod+Ctrl+Q |
| 信息侧栏 | Mod+Ctrl+N |
| 设置中心 | Mod+Ctrl+Comma |
| 灵动岛工具 | Mod+Shift+T |
| 更新与快照菜单 | Mod+F2 |
| 电源/GPU 菜单 | Mod+F3 |
| 显示/隐藏任务栏 | Mod+F4 |
| 手动锁屏 | Mod+Alt+L，继续使用 hyprlock |

Mod 是当前 niri 配置的 Super/Win 键。窗口操作、外接屏和 Firefox 显卡选择保持本机已有逻辑。

## 自己改和构建

源码在 `clavis/`，个人设置在 `~/.config/clavis/`。修改后先提交经过检查的源码，再构建和切换：

```bash
cd ~/Projects/HoloArch
git add <修改过的文件>
git commit -m "说明改动"
holoarch-shell build
holoarch-shell promote
```

构建创建新的本地发布目录，通过原生测试后才安装。`promote` 切换发布并重启 Shell。编辑源码不会直接改动正在使用的发布目录。

```bash
holoarch-shell log       # 查看启动日志
holoarch-shell rollback  # 回到上一个发布目录
holoarch-session legacy  # 恢复原桌面
holoarch-session clavis  # 切回 Clavis
```

上游更新由自己选择，具体命令、依赖和验收记录见 [部署与维护](docs/clavis-deployment.md)。

## 仓库内容

| 目录 | 内容 |
| --- | --- |
| `clavis/` | 完整 Shell 源码及本机 QML 适配，保留上游历史 |
| `key-cli/` | 固定 2026.9.25 后端源码与原生指标采样器 |
| `deploy/` | 构建脚本、模板选择及 AUR 构建定义 |
| `home/` | 本机配置、用户服务、输入法/终端配色和维护命令 |
| `profiles/legacy/` | 原桌面的 niri 启动和快捷键 |
| `system/` | 原有电源/GPU helper 与系统服务 |
| `inventory/` | 首次备份清单与此次部署记录 |
| `docs/` | 维护、恢复和调研 |

本仓库公开。初始设置已检查凭据；壁纸照片、聊天、剪贴板历史、账号/API 密钥、个人通知和运行截图保留本机。`inventory/backup-manifest.json` 描述迁移前归档，当前分支文件校验使用根目录 `SHA256SUMS`。

这些配置依赖本机用户名、固件和 niri 会话。在其他电脑使用前阅读 [恢复说明](docs/restore.md)。

Clavis/key-cli 原始 Git 历史、GPL 授权、第三方 notices 和资源授权均保留。自己的修改不改变原作者的署名与授权。
