# 维护方法

## 日常小改动

从 `AGENTS.md` 与当前状态定位模块，修改源码，跑对应测试；交付前执行完整主机回归。只更新当前状态中的变化和验证结果，不重复维护多份历史说明。

## 环境和命令

ESP-IDF 版本固定为 5.5.3。工程可在 macOS、Linux 或 Windows/WSL 中构建；`build.sh` 默认从 `/opt/esp-idf` 加载环境，其他安装位置通过 `IDF_DIR` 指定。进入项目目录后：

```bash
cd /path/to/passport-os
bash tools/test-host.sh
IDF_DIR=/path/to/esp-idf bash build.sh
```

主机测试需要 Python 3、C/C++ 编译器、Node.js 及脚本用到的常规工具。首次固件构建需按 `dependencies.lock` 准备 `managed_components/`；该目录不提交到 Git。

`build.sh` 每次从 `sdkconfig.defaults` 生成配置，避免旧 sdkconfig 破坏内存预算。不要删 CMake 的内存配置检查。`bash build.sh release` 是独立发布配置构建，不表示已经发布；`diag` 是诊断构建，不能当正式包。

固件交付检查（在载入 ESP-IDF 的 shell 中）：

```bash
idf.py -B build merge-bin -o FoloToy-AI-Passport-full.bin
python3 tools/verify_firmware.py build
```

`python3 tools/check_repo.py --firmware-fork` 做仓库检查；`bash tools/validate.sh --static` 还会运行 actionlint 和完整主机测试，首次缺工具时安装脚本需要联网。Git 远端与当前分支以 `docs/CURRENT.md` 为准。

## 设备更新

先确认实际串口和用户当前操作范围。用户明确要求全新安装时，可以在备份后全片擦除并写入完整镜像；其他升级默认保留现场资料；新鲜整片备份留在本地诊断目录，核对分区后仅更新 `0x10000` 应用分区，读回验证并确认非应用区保持。未获得全清授权时，不要全片擦除或将合并镜像覆盖已有设备资料。

构建产物位于忽略的 `build-public/`，不提交到 Git。交付时以 `docs/CURRENT.md` 记录的大小和 SHA256 为准。

### Flash 分区变更

分区偏移变化不能沿用“只更新应用分区”的流程。修改前先完整读取并校验 8 MiB Flash，备份 `cardid` 和 `storage` 中的有效文件；新应用可能覆盖旧布局所在的区域。迁移时分别写入匹配的 bootloader、分区表和应用，不要用从 `0x0` 开始的合并镜像覆盖合并空白区的 NVS；给移动后的 `cardid` 写入备份数据，并用相同 SPIFFS 参数重建目标尺寸的 `storage`，最后读回校验全部分区。

## 交接

`firmware/snapshot-manifest.json` 如存在，保存快照文件 SHA256 和来源提交，仅供追溯。源码快照不包含可再生成的 `build-public/` 和 `managed_components/`。
