# 桌面恢复

## 恢复前检查

本备份来自 `/home/zhuoran` 的 Arch / niri 配置。部分路径写了本机用户名；在其他用户账号上使用时，先检查绝对路径、显示器和 GPU 接口。

`home/` 保存了筛选后的桌面配置，`system/` 保存了本机电源控制文件。用户服务只收录 niri 的 EGL drop-in；软件包提供的 PipeWire 服务和与桌面无关的技能同步服务没有复制进来。输入法用户词库和个人文件保存在本机 home 快照中。

## Git 配置恢复

先关闭正在修改配置的设置程序，并为当前状态留一份备份。在仓库根目录预览：

```bash
rsync -a --dry-run --itemize-changes home/ "$HOME/"
```

确认之后恢复用户配置：

```bash
rsync -a home/ "$HOME/"
```

这会覆盖仓库收录的文件，不删除仓库没有收录的文件。默认不会创建快照、安装软件或重启桌面。恢复后先验证 niri 配置：

```bash
niri validate -c "$HOME/.config/niri/config.kdl"
systemctl --user daemon-reload
```

随后按需重启对应组件，或重新登录桌面。不要在恢复后立即运行 `holoarch update`；它会进行系统升级。

## 系统电源文件

先逐项检查 `system/` 中的文件，确认仍是这台机器和原固件接口。可查看系统恢复差异：

```bash
sudo rsync -a --dry-run --itemize-changes system/ /
```

确认需要恢复时再执行：

```bash
sudo rsync -a system/ /
sudo systemctl daemon-reload
```

恢复文件本身不会执行 GPU 切换命令；已有服务可能在下次启动时读取保存的电源偏好。真正设置电源/GPU 时继续使用 `holo-power-mode` 的检查和授权流程，确认是否需要重启。

## 软件与资源

- `inventory/pacman-all.txt`：已安装包和版本。
- `inventory/pacman-explicit.txt`：显式安装的所有包名。
- `inventory/pacman-official-explicit.txt`：显式安装且当前可由官方仓库识别的包名。
- `inventory/pacman-foreign-explicit.txt`：显式安装的外部包名，需要核查 AUR 或原安装来源。
- `inventory/flatpak-apps.tsv`：Flatpak 应用 ID、分支和来源。

壁纸原图、手工下载的 AppImage、独立程序和其他个人数据需要从本机 home 快照或原备份恢复。清单是记录，不自动重放所有安装命令。

## 本机原始归档

本次归档路径见 `inventory/backup-record.json`，权限为目录 0700、归档 0600。归档保留选定配置的原始内容，包含公开仓库排除的书签与输入法缓存。

先检查 SHA-256，再解压到临时目录检查：

```bash
tar -tzf /path/to/desktop-exact.tar.gz
mkdir -p /tmp/holoarch-restore-review
tar -xzf /path/to/desktop-exact.tar.gz -C /tmp/holoarch-restore-review
```

确认内容后按上面的用户/系统步骤恢复。归档目录布局同样是 `home/` 和 `system/`。

## Snapper 快照

本次分别创建 root 和 home 的 single 快照，编号与说明记录在 `inventory/backup-record.json`。快照没有设置自动清理算法，会保留到手动删除。

```bash
snapper -c root list
snapper -c home list
```

root 与 home 是两个独立子卷，创建时间接近但不是跨子卷原子事务。完整回退前应核查当前修改、独立子卷和引导方式，再选择恢复方法。这里只记录快照，不提供自动执行的整盘回滚脚本。
