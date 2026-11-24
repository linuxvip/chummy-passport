<p align="right">
  <a href="CONTRIBUTING.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Contributing

Read [AGENTS.md](../AGENTS.md), [docs/CURRENT.md](../docs/CURRENT.md) and
[docs/MAINTENANCE.md](../docs/MAINTENANCE.md) before changing the firmware.
They describe the current maintenance boundary, build commands and evidence
required for release.

Keep each change focused. Update the current status only when behavior or
verification changes, and do not add parallel historical status files.

Before opening a pull request:

1. Review the complete diff and remove unrelated or generated files.
2. Run `bash tools/test-host.sh`.
3. Run `bash tools/validate.sh --static`.
4. With ESP-IDF 5.5.3 activated, run `bash tools/validate.sh --firmware` for
   firmware changes.
5. Report host, firmware and physical-device results separately.

Do not commit Wi-Fi credentials, account cookies, access tokens, device
backups, personal photos or private logs. Report vulnerabilities through the
private process in [SECURITY.md](SECURITY.md).
