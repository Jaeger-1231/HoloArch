# HoloArch

这台 Arch / niri 电脑的桌面配置和维护脚本备份。首次备份日期：2026-10-01，状态为迁移到 Clavis 之前。

## 内容

| 目录 | 内容 |
| --- | --- |
| `home/` | niri、Waybar、通知、壁纸、配色、输入法、字体、终端和相关用户脚本 |
| `system/` | 本机电源/GPU helper、systemd 服务和电源偏好 |
| `inventory/` | 软件清单、逐文件 SHA-256、备份范围和快照记录 |
| `docs/` | 恢复方法与 Clavis 迁移调研 |

目录保留原始布局，例如 `home/.config/niri/config.kdl` 对应 `~/.config/niri/config.kdl`，`system/usr/local/libexec/holo-power-gpu` 对应 `/usr/local/libexec/holo-power-gpu`。执行权限和符号链接保留。

本仓库公开。上传内容经过凭据检查；个人 GTK 书签和输入法缓存仅保存在本机原始归档中。壁纸照片、聊天数据、浏览器账号、科研项目、Conda 环境和程序二进制由本机备份管理。Git 配置备份不等同于完整系统镜像。

## 恢复

先阅读 [恢复说明](docs/restore.md)。配置恢复前可预览差异：

```bash
rsync -a --dry-run --itemize-changes home/ "$HOME/"
```

软件清单记录已安装版本与来源；重新安装前应按当前 Arch 仓库检查包名。电源/GPU helper 依赖本机固件接口，使用其他电脑时需要重新适配。

本次的 root、home 快照与本机原始归档见 [备份记录](inventory/backup-record.json)。逐文件内容校验见 [backup-manifest.json](inventory/backup-manifest.json)。

## 自己维护

在 `home/`、`system/` 中维护自己需要的配置和脚本，通过 Git 提交记录修改。选定的文件更新后可重新生成 SHA-256 清单。新增配置先检查是否包含账号信息，再提交到公开仓库。

Clavis 移植方案见 [调研报告](docs/clavis-migration.md)。目前本仓库保存原有桌面作为恢复基线；后续实施可在独立分支进行。

原有脚本、模板和图标中的作者信息及授权说明应保留。此备份未为第三方文件重新声明统一许可证。

