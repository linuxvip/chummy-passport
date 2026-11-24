<p align="right">
  <strong>简体中文</strong> · <a href="CONTRIBUTING.md">English</a>
</p>

# 贡献指南

修改固件前先阅读 [AGENTS.md](../AGENTS.md)、[docs/CURRENT.md](../docs/CURRENT.md)
和 [docs/MAINTENANCE.md](../docs/MAINTENANCE.md)。这些文件定义了当前维护边界、
构建命令和发布证据。

每次修改保持单一目标。只有行为或验证结果变化时才更新当前状态，不再新增
并行的历史状态文档。

提交 PR 前：

1. 审查完整 diff，排除无关文件和生成文件。
2. 运行 `bash tools/test-host.sh`。
3. 运行 `bash tools/validate.sh --static`。
4. 固件修改在激活 ESP-IDF 5.5.3 后运行 `bash tools/validate.sh --firmware`。
5. 分开报告主机测试、固件构建和真机验收结果。

不要提交 Wi-Fi 密码、账号 Cookie、访问令牌、设备备份、个人照片或私有日志。
安全漏洞请按 [SECURITY.zh_CN.md](SECURITY.zh_CN.md) 私下报告。
