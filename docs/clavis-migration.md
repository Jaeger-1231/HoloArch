# Clavis 迁移与自主维护调研

调研日期：2026-10-01。目标：把 StatIndet/quickshell 的桌面界面迁移到本机，并让源码、个人设置和后续更新由自己管理。

后续实施已接入本机，实际适配与验证见 [部署记录](clavis-deployment.md)。以下保留最初调研时的判断与边界。

## 结论

本机具备迁移基础。建议把 Clavis 作为新的桌面界面，引入一组经过本机验收的 Clavis / key-cli 版本，在自己的 Git 仓库里保存适配代码和部署配置。以后按自己的节奏修改、打包、更新，上游仅作为可选择的改动来源。

这次完成的是源码、发布渠道、依赖和本机配置的调查；尚未完成本机编译、图形试运行或功能验收。以下“可以迁移”指技术路线成立，实际运行结果仍需实施阶段确认。

## 1. 仓库是什么

它叫 Clavis Shell，使用 Quickshell、QML、Qt 6 和原生 C++ 模块，为 niri 提供桌面界面。顶层源码装配了任务栏、Dock、Spotlight 启动器、信息与快捷设置侧栏、壁纸、桌面卡片、Keystone 面板、锁屏和电源菜单。它可以替换多项目前分散在 Waybar、mako、fuzzel、Waypaper 等工具里的界面；显示器、窗口布局和输入设备仍依赖 niri。[README](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/README.md)、[AppShell.qml](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/AppShell.qml)

审计的 Clavis main 提交为 `91cdecbe08831acdcff44d350e09232efe7164ed`，提交时间 2026-09-30 22:21:35 +0800，标题为“优化侧栏拖拽并新增浮动窗口视差”。本地只读源码位于 `/tmp/clavis-upstream-audit-20261001`，属于临时调研材料。

需要一起考虑另一个仓库 **key-cli**。它提供 Shell 生命周期、剪贴板、录屏、录音、键盘锁状态和系统指标等后端；Clavis 直接消费其 JSON/JSONL 协议。只复制 QML 无法得到完整功能。当前依赖清单声明 `key-cli>=2026.9.25`。[项目职责](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/AGENTS.md)、[依赖清单](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/packaging/dependencies.json)、[key-cli](https://github.com/StatIndet/key-cli)

## 2. 安装和更新渠道

只读 GitHub API 查询确认，两个项目均有已完成的正式 Release，查询时最新都是 `v2026.9.25`。Clavis 提供完整源码、PKGBUILD、SRCINFO、SHA256SUMS 和安装器；key-cli 还提供 wheel 和权限相关打包文件。已下载这两版的全部公开资产，逐项核对 SHA256SUMS，均匹配；这证明下载完整性，不等同于本机安装成功。[Clavis Release](https://github.com/StatIndet/quickshell/releases/tag/v2026.9.25)、[key-cli Release](https://github.com/StatIndet/key-cli/releases/tag/v2026.9.25)

完整发布源码中的精确提交记录：Clavis 为 `b54cd7c7fbf284f55234df511b3e4b0b5312fcc9`，key-cli 为 `f91a7aed7da1d3b37d1a722a7a85493053378755`。key-cli 当前 main 是 `3aed1bdeed204ad6e9714d676b5008943f69920c`；对照发布源码，其 `native/` 全部文件一致，`src/key_cli/` 仅版本字段不同，后续 main 变化主要在源码安装器与文档。因此初期可固定 key-cli 的 9/25 发布版，再验证所选 Clavis 版本；文件搜索、计算器、汇率和时区接口在该后端发布版已存在。其 wheel 仅有 Python 层，完整安装还要部署原生系统指标采样器。[key-cli 发布版](https://github.com/StatIndet/key-cli/releases/tag/v2026.9.25)、[key-cli main 源码](https://github.com/StatIndet/key-cli/tree/3aed1bdeed204ad6e9714d676b5008943f69920c)

作者的一键安装入口会选择正式 Release。它下载并验证源码与打包文件，在本机编译 Arch 包，再交给 pacman 安装；包含一次 `pacman -Syu`，并按默认完整功能清单安装依赖。交互安装的五项选项默认是 Yes，涉及键盘授权、CPU 功耗读取授权、Shell 与剪贴板自启动及立即启动。安装器本身不替换驱动或音频服务，不自动写入 niri 配置。[安装说明](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/installation.md)

**当前 main 与正式 Release 是不同的版本选择。** 若追求较容易复现的起点，可先评估两者的 9/25 正式版；若需要 main 新增界面与交互，应固定 Clavis main 和匹配的 key-cli 提交，并验证新增命令与协议。main 的 `VERSION` 仍为 `2026.9.12`，发布流程在临时发布提交中生成日期版本，所以源码维护应记录 Git 提交，不能仅靠此文件判断新旧。[发布流程](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/releasing.md)、[VERSION](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/VERSION)

官方说明没有增加应用更新器或后台发布轮询。重新执行安装器才会发现新的一方 Release；第三方依赖由 pacman/AUR 管理。对于自己的版本，可以自行构建并安装本地包，更新周期完全自己决定。[安装说明](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/installation.md)

## 3. 本机条件

以下来自本次实际读取的 pacman 数据库与配置文件，没有用旧记录代替当前核查：

- 系统：Arch Linux，niri `26.04-1`。
- Qt：主要 Qt 6 组件为 `6.11.2`，已安装 CMake 和 Ninja。
- niri 配置会启动 Waybar、mako、awww、Fcitx5、swayosd、Polkit 代理与 cliphist 捕获。
- 壁纸链路是 Waypaper → awww → 本机 Matugen 脚本，另有 overview 壁纸与显示器热插拔恢复。
- 本机电源控制脚本、GPU root helper、Firefox 显卡启动包装和 HoloArch 更新脚本仍存在。

用 `pacman -T` 检查当前 main 依赖清单中所有 `defaultInstall=true` 的独立依赖约束：67 项中已满足 53 项，缺 14 项。这里包含构建和可选功能，不能把 14 项全部当成最小启动要求。

| 分类 | 当前缺项 |
| --- | --- |
| 主要运行依赖 | quickshell、key-cli>=2026.9.25、libcava、qt6-m3shapes-git、ttf-material-symbols-variable |
| 天气动画与地图 | qt6-lottie、qt6-location、qt6-positioning、maplibre-native-qt |
| 构建和检查 | qt6-tools、shellcheck |
| 扩展功能 | fd、gpu-screen-recorder、rclone |

`libcava` 与 `qt6-m3shapes-git` 来自 AUR。已有 `cava` 可执行程序不能代替 `libcava` 库。依赖清单标明 Quickshell 验证基线为 0.3.1；仅有 Qt 组件仍不足以运行 Shell。[依赖说明](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/dependencies.md)

另查 key-cli 自身清单，本机还缺 Arch 运行依赖 `python-evdev>=1.7`、`python-pyudev>=0.24`，它们没有包含在上面 Clavis 的 67 项统计里。计算器可选依赖 `libqalculate`（提供 qalc）也缺，`tzdata` 已满足。安装时应递归检查后端清单。若采用 key-cli 的独立 venv，则应把 Python 依赖装入该环境，并在服务中固定入口，避免受到科研 Conda PATH 的影响。[key-cli 依赖清单](https://github.com/StatIndet/key-cli/blob/3aed1bdeed204ad6e9714d676b5008943f69920c/packaging/dependencies.json)、[Clavis 后端开发入口](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/development.md)

## 4. 本机需要适配的地方

| 部分 | 本机情况与建议 |
| --- | --- |
| 任务栏、通知、启动器 | 用 Clavis 接管对应界面，切换时处理 Waybar、mako、swayosd 等现有启动入口，防止重叠或重复服务。 |
| 电源与 GPU | 现有 `holo-power-mode` 已包含 CPU/固件档位、混合/独显切换、待重启状态和 Firefox GPU 选项。应把它抽成独立命令，由新界面调用。审计的 Clavis `PowerService` 主要读取电池，没有这套本机控制。 |
| 更新与快照 | 将 `holoarch update/list/refresh/snapshot` 接到新界面，或先保留独立终端/快捷键入口。Clavis 的包计数服务不足以替代本机已有维护流程。 |
| 壁纸与配色 | 选定一个日常管理入口，迁移现有模板与应用 hooks，解决相同生成目标被两套流程写入的问题。 |
| 双屏和 overview | Clavis 自己的 awww namespace 是 `clavis-desktop`；overview 是 `clavis-overview-wallpaper`。本机旧规则匹配 `awww-daemonoverview` / `swww-daemonoverview`，迁移需调整对应 layer 规则。 |
| 锁屏和闲置 | 本机手动/合盖锁屏调用 hyprlock，swayidle 自启动被注释。Clavis 默认 600 秒锁屏、900 秒熄屏，需明确采用哪套行为，并验收认证、合盖和恢复。 |
| 剪贴板 | 本机已有 `wl-paste --watch cliphist store`。引入 key-cli 的独立 clipboard service 后应统一捕获入口。 |
| 色温与屏幕共享 | 本机有 wlsunset、portal 和 niri 会话环境设置，应检查与 Clavis 色温控制及录屏功能的配合。 |

电源脚本当前把电源档位与显卡切换分开：`set {eco|balanced|performance}` 设置 CPU/平台档位，`set-gpu {hybrid|discrete}` 单独切换显卡。迁移应以这一轮读到的脚本为准。

依据：[PowerService](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/Services/PowerService.qml)、[IdleService](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/Services/IdleService.qml)、[壁纸后端](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/wallpaper-backends.md)、[配置所有权](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/architecture/config-isolation.md)

快捷键已有至少六处与 Clavis 默认键位相撞：

| 键位 | 本机现有动作 | Clavis 默认动作 |
| --- | --- | --- |
| Mod+Slash | 临时终端 | 快捷键配置图 |
| Mod+Alt+W | Waypaper | 壁纸选择 |
| Mod+Alt+V | HoloArch 剪贴板 | Clavis 剪贴板 |
| Mod+A | 向左收纳/移出窗口 | 快捷设置侧栏 |
| Mod+N | 切换浮动焦点 | 信息侧栏 |
| Mod+Shift+W | 聚焦微信 | Keystone 主面板 |

上游快捷键接入会检查冲突，有冲突时拒绝写入，因此实施时应先准备自定义片段，保留熟悉的窗口操作，并明确替换旧工具入口。[IPC 与默认键位](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/ipc.md)

## 5. 怎样自己维护

建议建立自己的源码与部署仓库，保留原提交历史，记录首次验收的 Clavis / key-cli 提交。上游 remote 可命名为 `upstream`，自己的仓库为 `origin`；取回上游信息和采用它的代码是两个步骤。

| 内容 | 管理方式 |
| --- | --- |
| Clavis 源码 | 自己修改 QML/C++，在独立测试 checkout 中验证，再从验收提交打包部署。 |
| key-cli | 首次固定一个兼容版本。需要改后端或长期独立维护时，保留自己的分支/镜像。 |
| 个人设置 | `~/.config/clavis/`，包括 config.json、闲置策略、UI 偏好等。 |
| niri 适配 | 独立保存启动、快捷键与 `clavis/*.kdl` 片段及原配置备份。 |
| 本机脚本 | 保留 HoloArch、电源/GPU、Firefox 启动和必要硬件适配，界面通过稳定命令调用。 |
| 已验证版本 | 保存提交号、依赖约束、构建包和对应配置快照，方便自己恢复。 |

日常更改颜色、位置和组件主要使用设置 UI/JSON；修改界面与交互需要 QML；修改原生 backend 需要 C++/CMake。QML 开发支持保存后热重载，C++ 插件变更需要重新构建和加载。源码入口 `~/.config/quickshell/clavis` 可指向 checkout，优先于系统安装入口；这很适合开发，但日常使用建议部署已验收的版本，避免测试分支编辑直接作用于桌面。[开发流程](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/development.md)、[路径定义](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/Common/Paths.qml)

查看可选上游改动的日常命令可以很简单：

```bash
git fetch upstream
git log --oneline HEAD..upstream/main
```

选择需要的提交后，在测试分支合并或挑选，核查对应 backend 需求；通过构建和相关验收后更新自己的部署。自己的更新工具应固定本仓库的源码与打包定义。上游安装器硬编码作者的发布渠道，原样用于日常更新会重新选择作者的版本。[安装器源码](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/scripts/install/arch.py)

主项目打包声明使用 GPL-3.0-or-later；仓库还保存了第三方组件、字体和素材的授权说明。自己的衍生仓库应保留 LICENSE、原作者署名和相关 notices。[打包授权声明](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/packaging/arch/PKGBUILD.in)、[第三方授权映射](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/licenses/README.md)

## 6. 建议的实施顺序

1. 备份当前 niri、Waybar、通知、壁纸、Matugen、锁屏配置与用户服务；保存可恢复的启动方案。
2. 选定并记录一组 Clavis / key-cli 版本；建立自己的源码分支和部署配置。
3. 补齐所选版本的依赖，准备完整资源，再编译并查看实际启动日志。
4. 在隔离的配置和可恢复的会话中试运行。Clavis 提供 `CLAVIS_CONFIG_HOME`、数据/状态/缓存目录覆盖，但这不自动隔离 GTK、Kitty、Yazi 等模板输出；试运行时先限制外部模板写入。
5. 接入自己的电源/GPU、HoloArch、快捷键和壁纸配色；确定锁屏、闲置和剪贴板的唯一管理入口。
6. 验收中文与输入法、任务栏/托盘、通知、启动器、剪贴板、双屏热插拔、overview、锁屏解锁、媒体与录屏；再切换日常自启动。

一个容易遗漏的源码细节：Git checkout 的天气素材目录只有占位文件，完整 Meteocons 素材由 release-source 工具按固定下载地址和 SHA-256 打包。自行维护 main 时应复用 `scripts/release.py bundle-resources --cache <缓存目录>` 的资源准备流程，或使用已验证的完整源码资产；普通 CMake 构建不会自动补资源，GitHub 自动生成的 Source code 压缩包不等同于项目发布的完整源码资产。[发布说明](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/docs/releasing.md)、[资源准备工具](https://github.com/StatIndet/quickshell/blob/91cdecbe08831acdcff44d350e09232efe7164ed/scripts/release.py)

## 本次核查边界

- 已阅读当前源码、安装/发布机制、依赖清单和本机相关配置。
- 已只读查询两个项目的正式 Release，下载公开资产并验证 SHA-256。
- 已通过 pacman 依赖检查确认当前清单的本机缺项。
- 没有本机构建、图形试运行或硬件功能验收结果；不能据此承诺零适配问题或完全复现截图。
