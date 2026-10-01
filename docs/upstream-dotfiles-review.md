# StatIndet/dotfiles 配置研究

核查日期：2026-10-02。范围：作者公开仓库的完整文件树及 81 个小型配置文件，逐个核验下载内容的 Git blob SHA；未下载壁纸、输入法键盘缓存，未安装软件、未覆盖本机配置。

## 来源与版本

作者的另一个仓库是 [StatIndet/dotfiles](https://github.com/StatIndet/dotfiles)，提供桌面周边软件配置。GitHub 当前标明作者于 **2026-10-01 归档**；默认分支 `master` 的最新提交仍是 [`6947cdf70cddcf3d22532535ff5f47b5b434e170`](https://github.com/StatIndet/dotfiles/commit/6947cdf70cddcf3d22532535ff5f47b5b434e170)，提交时间为 2026-05-24，内容是注释内屏 `eDP-1` 的 `off`。这正是之前提取 Fastfetch 时使用的版本。[提交元数据](https://api.github.com/repos/StatIndet/dotfiles/commits/6947cdf70cddcf3d22532535ff5f47b5b434e170)

下文源码链接全部固定到该提交。完整文件树没有截断，共有 **21 个顶级目录**；其中 `quickshell` 是 Git gitlink，引用 `4e9ef923ddb5ebe049880305f93946440d40a684`，不含可以独立复制的 QML 源码树，根目录也没有 `.gitmodules`。[完整文件树](https://api.github.com/repos/StatIndet/dotfiles/git/trees/6947cdf70cddcf3d22532535ff5f47b5b434e170?recursive=1)

## 重点：自动配色与模糊

### 壁纸能触发配色，但此仓库没有 Kitty 自动配色

作者的旧壁纸选择流程为：Rofi 显示壁纸缩略图 → `swww img` 切换壁纸 → `matugen image "$SELECTED"` 提取颜色 → 调用 `overview.sh` 生成总览及模糊壁纸缓存。[壁纸脚本](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/swww-rofi/swww-rofi.sh)

Matugen 实际启用的输出有 Rofi、Cava、niri、Fcitx5、Hyprlock 颜色文件、btop、Quickshell 颜色 JSON、Yazi。Waybar 的输出段落已注释；**没有 Kitty 模板，没有 `kitten @ set-colors` 或 Kitty 重载 hook**。[Matugen 配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/matugen/config.toml)

| 输出 | 是否有实际引用 | 刷新方式及限制 |
| --- | --- | --- |
| niri 聚焦边框、最近窗口高亮 | `config.kdl` 包含 `colors.kdl`，生成文件定义渐变边框和高亮色 | 没有重载 hook；靠 niri 对配置的读取。生成路径与引用路径一致。[配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri/config.kdl)、[模板](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/matugen/templates/niri-colors.kdl) |
| Cava 渐变 | 覆写 Cava 配置，底部是两级渐变 | `pkill -USR1 cava`；这也是整个配置文件模板，会覆盖后续手工修改的 Cava 参数。[模板](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/matugen/templates/matugen-cava) |
| Fcitx5 候选框 | `classicui.conf` 选择 `Theme=Matugen`，模板写到用户主题目录 | `fcitx5 -r -d & disown`；迁移时应改成与实际 shell 和会话一致的刷新方法。不要覆盖本机刚修好的背景图片模板。[主题选择](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/fcitx5/conf/classicui.conf)、[模板](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/matugen/templates/fcitx5-theme.conf) |
| btop | `color_theme="matugen.theme"` | 配置启用该生成主题，但没有运行中重载 hook。[btop 配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/btop/btop.conf) |
| Yazi | 直接写入 `~/.config/yazi/theme.toml` | 只有配色，没有功能插件和快捷键配置。[模板](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/matugen/templates/yazi-theme.toml) |
| Rofi | `config.rasi` 导入生成的 `colors.rasi` | 部分边框、选中等颜色使用生成值，`theme.rasi` 中主背景等仍是静态 Catppuccin 色值，不能说全界面都跟壁纸。[配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/rofi/config.rasi)、[静态主题](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/rofi/theme.rasi) |
| Quickshell | Matugen 写出 `~/.cache/quickshell_colors.json` | 模板是全部颜色 token 的 JSON；对应消费与刷新逻辑需看独立 Quickshell 仓库。[模板](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/matugen/templates/Colorscheme.qml) |
| Hyprlock | `hyprlock.conf` 引用生成的 `hypr/colors.conf` | 当前旧仓库锁屏命令实际调用 Quickshell，不能据此认定 Hyprlock 正在使用。[Hyprlock](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/hypr/hyprlock.conf)、[Hypridle](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/hypr/hypridle.conf) |

Kitty 在这个旧仓库里明确使用 `/usr/bin/zsh`、JetBrainsMono Nerd Font 15 号及中文 LXGW WenKai 字体映射，导入的 `current-theme.conf` 是 **静态 Catppuccin Mocha**；`background_opacity 0.8` 和 `background_blur 30` 都有 `#` 注释。因此，作者录像里的新版终端效果不能仅凭这个目录推断，当前 HoloArch 的 Fish/Starship/Matugen 终端适配也不应被这个旧 Kitty 配置覆盖。[Kitty 配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/kitty/kitty.conf)、[静态颜色](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/kitty/current-theme.conf)

### 模糊主要是壁纸预处理，未配置全局窗口背景模糊

作者 niri 目录只有 8 个文件，没有 `blur.kdl`，没有 `background-effect`、`xray`、`popups` 或背景模糊规则。实际窗口规则是圆角 8、裁切到圆角、禁止边框画进背景、指定工具窗口浮动；全局 `opacity 0.97` 也被注释。`config.kdl` 的 `shadow.softness 30` 是阴影参数，且启用阴影的 `on` 已注释，不能当成背景模糊。[niri 文件树](https://github.com/StatIndet/dotfiles/tree/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri)、[窗口规则](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri/windowrule.kdl)、[主配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri/config.kdl)

真正配置的模糊有两种：

1. **总览/锁屏图片缓存**：`overview.sh` 用 ImageMagick 生成 `-blur 0x15` 并暗化 40% 的总览图，另生成 `-blur 0x30` 的壁纸图；通过名为 `overview` 的 swww 实例显示总览图，并保存到 `~/.cache/wallpaper_rofi/{current,blurred}`。这是模糊一张壁纸，不会实时模糊窗口背后的其他应用。[脚本](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/swww-rofi/overview.sh)、[总览 layer 规则](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri/output.kdl)
2. **Hyprlock 锁屏背景**：配置包含 `blur_size=3`、`blur_passes=2`、噪声、对比度、亮度和饱和度参数。这只作用于 Hyprlock，仓库 `hypr` 下也没有 Hyprland 主配置；作者当前锁屏启动路径是 Quickshell IPC，并不调用 Hyprlock。[锁屏背景](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/hypr/hyprlock.conf)、[实际锁屏命令](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/hypr/hypridle.conf)

旧配置还有 `awww-daemon` 与 `swww` 命令混用、硬编码作者 `/home/archirithm` 路径、以文件名命中壁纸缓存（同名文件/原图更新时可能过期）等问题。可借鉴“总览用暗化模糊壁纸”的效果，但应接到当前 Clavis 的壁纸和配色流程，不能直接复制脚本。[启动项](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri/startup.kdl)、[旧壁纸流程](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/swww-rofi/swww-rofi.sh)、[缓存流程](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/swww-rofi/overview.sh)

## 每个顶级目录实际配置了什么

| 目录 | 实际内容与亮点 | 依赖 / 搬迁判断 | 固定提交源码 |
| --- | --- | --- | --- |
| `QDirStat` | 4 个配置：磁盘占用树图、按文件类型着色、排除 `.snapshot`、清理菜单 | 依赖 QDirStat；工具本身有用，作者配置多为窗口状态/默认设置。清理菜单包含无需确认的压缩后 `rm -rf`、递归删备份、`git clean -dfx` 等，宜用软件默认配置再按需求设置。 | [界面](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/QDirStat/QDirStat.conf)、[清理命令](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/QDirStat/QDirStat-cleanup.conf) |
| `Unknown Organization` | Qt 保存的一项 `quickshell.conf`，仅 `isRestoring=false` | 应用状态，没有可迁移功能。 | [文件](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/Unknown%20Organization/quickshell.conf) |
| `btop` | btop 布局/监控配置与 Matugen 主题；CPU/GPU/内存/磁盘/网络/电池、电量瓦数、CPU 瓦数、温度、频率，GPU 自动检测 | 依赖 btop，部分硬件指标还依赖机器接口；本机若已有同类监控及配色，应只对比新增指标，不覆盖完整配置。`theme_background=true` 会用主题背景。 | [配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/btop/btop.conf)、[主题](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/btop/themes/matugen.theme) |
| `cava` | 标准音频频谱配置、两级 Matugen 渐变、6 个 GLSL shader 文件、两个额外颜色主题 | 依赖 Cava；shader 文件存在，但当前 `config` 的输出方式/shader 选择均为注释，不表示正在用 shader。值得保留配色模板思路。 | [配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/cava/config)、[shader 目录](https://github.com/StatIndet/dotfiles/tree/6947cdf70cddcf3d22532535ff5f47b5b434e170/cava/shaders) |
| `environment.d` | 只有 `QT_QPA_PLATFORMTHEME=gtk3` | 会影响 Qt 软件的主题平台；仓库没有 Qt5ct/Qt6ct 配套配置。不可替换本机已适配的 Qt/输入法/Wayland 环境。 | [环境变量](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/environment.d/envvars.conf) |
| `fastfetch` | 图片在左、两组框线系统/硬件信息在右、彩色图标；另有 Shelby 文字 logo | 依赖 Fastfetch；原图片 `/home/archirithm/Pictures/new_world.jpg` 未收录，开头 `hyprctl splash` 不适用于 niri。本机已从此处改出随机私人竖图、居中裁切和自动启动。 | [配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/fastfetch/config.jsonc)、[文字 logo](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/fastfetch/shelby_logo.txt) |
| `fcitx5` | Rime+US 键盘输入组、Ctrl+Space/Shift 切换、上下翻页/Tab 候选键、Matugen 候选框及霞鹜文楷字体 | 依赖 Fcitx5/Rime/霞鹜文楷；没有 Rime schema、用户字典等方案。键盘缓存不搬；保留本机当前方案和微信修复。 | [输入组](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/fcitx5/profile)、[快捷键](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/fcitx5/config)、[主题](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/fcitx5/conf/classicui.conf) |
| `fontconfig` | Sans/Serif 优先霞鹜文楷，Monospace 优先霞鹜文楷等宽 | 字体包没有收录；会改全系统字体选择。偏好型设置，可单独在候选框/中文回退中采用，不宜直接替换既有 Noto 和编程字体。 | [字体规则](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/fontconfig/fonts.conf) |
| `hypr` | `hypridle.conf`、`hyprlock.conf`、生成的颜色；并没有 Hyprland 窗口管理器配置 | Hypridle+Quickshell IPC：5 分钟闲置锁屏、睡眠前锁屏、唤醒点亮 niri 屏幕；其余自动暗屏/挂起均注释。Hyprlock 有旧用户名、字体和布局残留，不能拿来直接启动。 | [闲置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/hypr/hypridle.conf)、[锁屏](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/hypr/hyprlock.conf) |
| `kitty` | 静态 Catppuccin Mocha，Zsh，15 号 Nerd Font，中文霞鹜文楷映射，隐藏窗口装饰 | 没有 Fish/Starship 配置，没有自动 Matugen 输出，透明/模糊选项未启用。保持本机已验收的终端。 | [配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/kitty/kitty.conf) |
| `matugen` | 1 个路由配置、9 个颜色模板；将壁纸颜色统一输出到周边应用和 shell JSON | 依赖 Matugen 和各应用；适合逐个增加当前链路缺的消费者，尤其 btop/Cava/Yazi，而非照搬作者旧路由。 | [模板路由](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/matugen/config.toml) |
| `niri` | 主配置、快捷键、颜色、光标主题、输出、启动、窗口规则、动画；16px 间隙/半屏默认列宽/8px 圆角/弹簧动画/工具窗口浮动 | 依赖 niri、Quickshell、awww、Fcitx5、polkit-gnome、nm/blueman、Hypridle、clipse；多处作者路径和旧 IPC。输出硬编码 eDP-1 2560×1440@240、DP-1 @180.001 及位置；不能覆盖本机165Hz和144Hz输出。 | [窗口](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri/windowrule.kdl)、[输出](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri/output.kdl)、[动画](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri/animations.kdl)、[启动](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/niri/startup.kdl) |
| `nvim-lazyvim-backup` | LazyVim 备份，Catppuccin、自动浅深色，clangd/CMake/Python/mini-hipatterns extras、lazy 插件锁定文件 | 依赖 Neovim/LazyVim/语言工具链；示例 `example.lua` 开头直接 `return {}`，其示例插件未启用。不优先迁移给主要用 VS Code 的用户。 | [extras](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/nvim-lazyvim-backup/lazyvim.json)、[颜色](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/nvim-lazyvim-backup/lua/plugins/colorscheme.lua)、[自动深浅](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/nvim-lazyvim-backup/lua/plugins/auto-dark-mode.lua) |
| `quickshell` | 指向独立源码提交的 gitlink，不是本目录已完整保存的 shell | 本机已迁移独立 Clavis 版本；不应退回旧指针。 | [目录引用](https://github.com/StatIndet/dotfiles/tree/6947cdf70cddcf3d22532535ff5f47b5b434e170/quickshell) |
| `rofi` | 带左侧当前壁纸、应用/命令/文件/窗口模式、Nerd 图标、主题和部分动态颜色 | 依赖 Rofi、JetBrainsMono Nerd Font、Tela-circle-dracula 图标主题及缓存壁纸；是 Clavis/Fuzzel 启动器的替代外观，不是新增必需功能。 | [布局](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/rofi/config.rasi) |
| `swww-rofi` | 壁纸缩略图选择器、swww 过渡、Matugen 提色、总览/锁屏模糊壁纸缓存 | 依赖 Bash、Rofi、swww、ImageMagick、Matugen、notify-send；硬编码作者路径且 swww/awww 混用。视觉思路可借鉴，不直接搬脚本。 | [选择器](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/swww-rofi/swww-rofi.sh)、[预处理](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/swww-rofi/overview.sh) |
| `wallpaper` | 15 张 JPG/PNG 壁纸，总计约 46.7 MB，未包含 Fastfetch 引用的 new_world.jpg | 可手动挑视觉素材；原图署名/许可不齐，不作为自有公共仓库默认发布资源。此轮未下载。 | [图片清单](https://github.com/StatIndet/dotfiles/tree/6947cdf70cddcf3d22532535ff5f47b5b434e170/wallpaper) |
| `waybar` | 30px 顶栏、niri 工作区、18条 Cava 频谱、中央时间、硬件抽屉、电池/托盘/网络/音量/电源 | 依赖 Waybar、Cava、pavucontrol、nm-connection-editor、wlogout 等；温度 `thermal-zone=4` 是主机专用；更新点击直接 `sudo pacman -Syu`，相关模块在主布局被注释。niri 启动 Waybar 也已注释，本机 Clavis 已替代顶栏。 | [布局](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/waybar/config)、[模块](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/waybar/modules.json)、[音频脚本](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/waybar/cava.sh) |
| `wlogout` | 锁屏/退出/关机/重启四按钮；hover 改圆角/图标大小的动效及图标素材 | 依赖 wlogout；硬编码作者 CSS/图标路径和固定数百像素 margin；本机已有电源界面，无需再加第二套。 | [动作](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/wlogout/layout)、[外观](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/wlogout/style.css) |
| `xdg-desktop-portal` | 默认 portal 为 `gnome;gtk;`，文件选择器固定 GTK | 与屏幕共享/文件选择器有关，需按当前会话 backend 选择；不复制覆盖当前经过适配的 portal。 | [配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/xdg-desktop-portal/niri-portals.conf) |
| `yazi` | 只有 `theme.toml`，颜色涵盖目录、选中/复制/剪切标记、标签、状态、输入框等 | **没有插件、快捷键、预览规则和 opener 配置**；可借鉴主题，不能从本仓库获得完整 Yazi 工作流。 | [唯一配置](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/yazi/theme.toml) |

根目录另有 `DemoSwitch.qml`（独立 QtQuick 动画开关演示，未被 niri 启动项引用）、极短 README 和 `.gitignore`。没有 Fish、Starship、Zsh dotfiles、Zathura 配置、GPU 切换脚本、系统安装脚本或统一软件清单；Kitty 的 `shell /usr/bin/zsh` 不等于仓库提供了 Zsh 提示符配置。[演示](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/DemoSwitch.qml)、[README](https://github.com/StatIndet/dotfiles/blob/6947cdf70cddcf3d22532535ff5f47b5b434e170/README.md)、[完整文件树](https://api.github.com/repos/StatIndet/dotfiles/git/trees/6947cdf70cddcf3d22532535ff5f47b5b434e170?recursive=1)

## 值得迁移的顺序

1. **先补齐当前配色链的消费者**：对照本机 Matugen/Clavis 已有路由，确认 Kitty、btop、Cava、Yazi 的实际应用和刷新是否都随壁纸。旧仓库可提供模板，但 Kitty 自动配色需要看当前独立 shell 的实现或自行补路由，不能声称旧仓库已实现。
2. **可借鉴总览模糊壁纸效果**：先使用当前 Clavis 已提供的总览模糊/暗化设置；保留 Kitty 防闪烁规则和微信候选框修复。作者旧脚本不能提供实时窗口模糊策略。
3. **按需做中文字体回退**：在用户偏好的应用设置霞鹜文楷，可先确认字体实际安装和显示效果；维持编程等宽字体和现有全局规则。
4. **QDirStat 是可选新工具**：用于直观看磁盘谁占空间；无需搬它的清理动作或窗口状态。
5. **本仓库没有 Yazi 功能套件可搬**：若希望文件搜索、图片/PDF预览、解压/打开工作流，应作为 HoloArch 自有配置另行设计，并与当前 Yazi 版本对照。

## 本机对照：自动配色与模糊现在到哪一步

以下来自 2026-10-02 读取本机配置和已部署源码的结果，本轮没有改变桌面设置或安装软件。

| 项目 | 本机已核查的状态 | 建议 |
| --- | --- | --- |
| Kitty 自动配色 | Clavis `WallpaperService` 切换壁纸后调用 `ThemeService.generateFromWallpaper`；`kitty` 模板已启用，生成 `~/.config/kitty/themes/Matugen.conf`，hook 复制到 `current-theme.conf` 并发 USR1 热重载。Kitty 引用该文件；两份颜色文件内容一致。 | 已接通，继续使用当前链路。旧 dotfiles 的静态 Kitty 文件没有新增功能。 |
| 提示符、Fastfetch | Starship 使用配色模板；Fastfetch 使用 Kitty ANSI 色板。已保留用户指定的随机竖图、对齐/裁切、自动启动和手动 `fa`。 | 保留当前适配。作者完整 Fish 提示符另有 `clavis-fish-theme` 项目，应作为独立外观选择。 |
| btop、Cava、Yazi、输入法 | Clavis 外部模板都已启用；btop 指向 `matugen.theme`，Cava 指向生成的 `your-theme`，Yazi 使用生成的 `theme.toml`，Fcitx 使用 Matugen 并带微信不透明背景修复。 | 已有对应配色消费者，继续核对换色时的实际显示即可；Yazi 功能扩展属于另行配置。 |
| 普通/浮动窗口 | `~/.config/niri/blur.kdl` 的通用规则为 `xray true`、`blur false`。Kitty 的专用规则为 `xray false`、`blur false`，Kitty 自身不透明度为 0.8。 | 半透明效果和实时背景模糊应分别判断；当前没有给普通窗口打开实时模糊。保留 Kitty 的已验收防闪烁例外。 |
| 文件管理器弹窗 | 旧规则对 `thunar`/`Nautilus` 配置了 0.92 不透明度、15 像素圆角和模糊；应用名匹配需按实际 `app_id` 核对。 | 下一轮实施时补齐应用 ID 匹配，先做单个窗口验收。 |
| Clavis 面板背景 | 本机 `effects.shellBlurEnabled=false`；新版已提供背景模糊及仅模糊壁纸的开关。 | 可在设置中的“常规 → 背景效果”接入 niri 效果并启用。优先从面板开始调整。 |
| Overview 总览背景 | 已启用总览背景，但 `blurRadius=0`、`dim=0`。新版 Clavis 有 0–100 的模糊和暗化滑块。 | 可在壁纸设置的总览区域调整；无需增加旧 Rofi/swww 脚本。 |
| 中文字体 | Noto Sans/CJK、Noto Serif/CJK、JetBrains Maple Mono 和 LXGW WenKai GB Screen 实际可匹配。现有全局规则使用 Noto/编程字体，Fcitx 字体为 Sans Serif 11。 | 若喜欢作者字体，可仅试中文回退或候选框，先比较阅读效果。 |

本机源码入口：[壁纸触发](../clavis/Services/WallpaperService.qml)、[配色生成](../clavis/Services/ThemeService.qml)、[Kitty 输出和热重载](../clavis/matugen/config.toml)、[模板选择](../deploy/matugen-selection.json)、[Clavis 模糊开关](../clavis/Modules/ControlCenter/GeneralEffectsPage.qml)、[总览效果设置](../clavis/Modules/ControlCenter/WallpaperPage.qml)。已备份的本机规则：[窗口模糊配置](../home/.config/niri/blur.kdl)、[个人配色输出](../home/.config/clavis/matugen/config.toml)、[Kitty 配置](../home/.config/kitty/kitty.conf)。

针对用户本次重点，最有价值的后续实施顺序是：**先验收已接通的终端随壁纸调色，再调整 Clavis 面板和总览模糊，最后补齐具体应用的弹窗规则**。本轮只完成研究，尚未开启新的视觉效果。
