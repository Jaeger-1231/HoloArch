# Clavis 本机部署与维护

部署日期：2026-10-01。使用自己的 HoloArch 仓库构建和部署，更新来源由自己决定。

## 固定版本

- Clavis 上游：`91cdecbe08831acdcff44d350e09232efe7164ed`。
- key-cli：`f91a7aed7da1d3b37d1a722a7a85493053378755`，版本 `2026.9.25`。
- Quickshell：Arch `0.3.1-1`；Qt 主要组件 `6.11.2`。
- libcava：`1.0.0-1`；M3Shapes：`r50.8a6fe89-1`。审阅过的 AUR 定义在 `deploy/aur/`。
- 两个源码目录使用 Git subtree 导入，保留原历史和授权。

## 日常部署

Shell 读取 `~/.local/share/holoarch/current` 指向的发布。`~/.config/quickshell/clavis` 指向发布内 `share/quickshell/clavis`，原生插件从发布内 `lib/qt6/qml` 加载。

后端在 `~/.local/share/holoarch/key-current/venv`。`~/.local/bin/key` 固定系统 Python venv 与 PATH，隔离科研 Conda。Python 代码以普通 wheel 安装，原生 `key-sysmon` 一起部署，运行时不读取开发 checkout。

## 本机接入

| 部分 | 当前设置 |
| --- | --- |
| 电源/GPU | 独立 holo-power-mode；面板显示实际模式、显卡及 Firefox GPU，打开原控制逻辑的菜单 |
| 维护 | 独立更新检查器；面板和 Mod+F2 打开检查、更新、快照、刷新；Kitty --hold 保留错误 |
| 自启动 | clavis-shell、clavis-clipboard、holoarch-updates.timer 跟随 niri.service |
| 剪贴板 | key-cli 捕获进入已有 cliphist 数据库；重复捕获停止，历史未清空 |
| 壁纸 | Clavis Quickshell 渲染，沿用原照片和文件夹；旧 awww/overview 停止 |
| Overview | 匹配 clavis-overview-wallpaper，layout 透明背景 |
| 配色 | Clavis 生成内部色板和 15 个个人模板，关闭内置 btop/cava/kitty/yazi，避免重复目标 |
| 色温 | Clavis 沿用 5000K/6500K、36.5/128.0、30 分钟过渡；wlsunset 停止 |
| 锁屏 | Mod+Alt+L 与合盖仍使用 hyprlock；自动闲置策略关闭，保持原行为 |
| 窗口管理 | 保留窗口操作键及 niri-sidebar 窗口收纳 |

旧 Waybar 脚本路径作为包装入口调用独立后端，旧桌面与新桌面使用同一套维护逻辑。

外部模板包括 Firefox/pywalfox、fuzzel、Kitty、Fcitx5、btop、cava、Starship、Yazi、GTK3/4、niri、GTK 图标、Fastfetch、hyprlock、OBS。Fcitx 改为配置重载。旧 Waybar/mako/swayosd 模板不启用；VS Code 注入模板保留在原备份，可自行选择接入。

## 验收与保留项

- Clavis 原生编译成功；现有 CTest 合计 **27/27 通过**。首次沙箱执行受到本地 socket 和 /run 写入限制，受影响的 9 项在允许该访问后重跑通过。
- key-cli 编译成功，现有原生测试 **2/2 通过**。
- 修改的 QML 格式/加载检查通过；qmllint 有 **42 条 advisory warning**，主要涉及已有动态类型、Appearance 属性推断及 Quickshell 信号类型，不是零警告。
- HDMI-A-1 和 eDP-1 均有任务栏、Dock、壁纸、overview 和 Keystone；中文快捷设置和启动器实际打开并查看截图。
- Shell、剪贴板与更新定时器 active/enabled；正式会话观察无自动重启。
- 通知 D-Bus 名称由 Clavis Quickshell 持有，旧对应进程停止。
- key clipboard status 为 available/watcherRunning=true；录音/录屏状态接口可用。
- 最终 niri validate 通过；实际维护状态为“平衡 / 混合显卡 / Firefox 集显”。
- 实际测试 legacy 恢复：旧任务栏、通知、剪贴板服务启动，双屏恢复原壁纸；已切回 Clavis。旧 Waypaper 位于 Conda 前缀，恢复工具直接调用 awww 和原配色/overview 脚本。
- 已提交源码的正式构建再次通过部署门禁：27/27 CTest。

键盘 Caps/Num Lock 原始 evdev 访问与 CPU 能耗 helper 授权未启用，键盘灯后端不可用、部分功耗指标为空。key doctor.runtimeReady 因键盘能力未启用为 false，其他所需命令无缺项。

Clavis 原生锁屏的密码解锁、真实合盖/恢复、显示器物理热插拔及实际录音/录屏交互需要本人验收。本次未切换 GPU、重启或执行系统更新。初次不存在的头像/历史文件会有日志提示，界面使用默认头像。

截图在 `~/.local/state/holoarch/verification/`，日志在 `~/.cache/holoarch-build/`，均保留本机。

## 部署后的恢复点

2026-10-01 21:10:34 创建并核对 Snapper 单次快照：**root #1126、home #918**，描述 `holoarch-clavis-ready-20261001-211034`，无自动清理规则。迁移前 **root #1119、home #915** 继续保留。

本机备份记录及 Git bundle 位于 `~/.local/state/holoarch/backups/20261001-211034-clavis-ready/`。个人运行状态、截图和私密配置留在本机；公开仓库保存源码、部署配置与验收记录。

## 修改与发布

日常偏好通过设置 UI 或 `~/.config/clavis/` 修改。界面代码在 clavis/Modules，系统交互在 clavis/Services；先阅读该源码的 AGENTS.md 和对应开发文档。

QML 改动先运行 `clavis/scripts/dev/format-qml.sh`，再按约定运行 `clavis/scripts/dev/check.sh`。需要图形会话的 tooling 在本机终端执行。确认源码后提交、运行 `holoarch-shell build`，成功后 `holoarch-shell promote`；失败保留当前发布。

```bash
holoarch-shell log
holoarch-shell rollback
```

Qt/Quickshell 大版本变化后重建 Clavis。系统 Python 大版本变化后运行 `deploy/build-key.sh` 重建后端，并重启两个 Clavis 用户服务。后端生成独立目录后才切换 key-current，不自动授予额外设备权限。

## 选择上游更新

```bash
cd ~/Projects/HoloArch
git fetch clavis-upstream main
git log --oneline 91cdecbe08831acdcff44d350e09232efe7164ed..clavis-upstream/main
git fetch key-upstream main
git log --oneline f91a7aed7da1d3b37d1a722a7a85493053378755..key-upstream/main
```

查看更新不改变自己的源码。确认要采用的提交后，在独立分支合并 subtree、解决适配冲突并验证：

```bash
git switch -c update-clavis
git subtree pull --prefix=clavis clavis-upstream <选择的提交或分支>
```

key-cli 对应使用 `--prefix=key-cli key-upstream`。记录新基线及依赖。不要把作者最新 Release 安装器接入自己的日常更新。

克隆后手动增加 upstream remote：

```bash
git remote add clavis-upstream https://github.com/StatIndet/quickshell.git
git remote add key-upstream https://github.com/StatIndet/key-cli.git
```

## 恢复旧桌面

```bash
holoarch-session legacy
holoarch-session clavis
```

切换前会把当前 niri 主配置和快捷键保存到带时间戳的本机目录，验证候选配置，再改变服务与启动入口。保留软件和个人文件。迁移前的归档、Git bundle 和 root/home Snapper 恢复见 [restore.md](restore.md)。

## 初次重建

先按 inventory/clavis-deployment.json 核对官方依赖和两个 AUR 定义，在系统终端安装缺失项。资源工具按固定 SHA-256 下载天气素材。

```bash
deploy/build-key.sh
deploy/build-clavis.sh
```

把所需 home/.local/bin、home/.local/libexec、Clavis 用户服务和个人模板部署到本机路径；创建 current 与 Quickshell 配置链接后使用 holoarch-session clavis。首次重建涉及机器路径和管理员软件安装，应逐项检查，不直接把 system/ 安装到其他电脑。
