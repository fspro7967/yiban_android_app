颐伴老人端 · 桌面联调版
========================

来源：feat_B2_platform @ 940e58d（2026-11-22）
打包方式：yiban_android_app.exe + Qt 6.11.0 / MinGW 运行库 + QML 插件，已用 windeployqt 部署。

下载地址
--------
仓库内文件：release/yiban-desktop.zip
https://github.com/fspro7967/yiban_android_app/blob/feat_B2_platform/release/yiban-desktop.zip

Release 镜像（同一个包）
------------------------
https://github.com/fspro7967/yiban_android_app/releases/tag/desktop-20261122

运行方法
--------
1. 下载并解压 yiban-desktop.zip 到任意目录。
2. 双击 yiban_android_app.exe。

云端连接
--------
- 内置服务器地址：http://101.245.109.50:8000/v1
- 令牌保存在本机 QSettings：HKCU\Software\Yiban\YibanElder\
  - net/token：ses-… 令牌（30 天）
  - net/paired：true
  - net/role：elder
  - net/deviceId：模拟设备标识
- 换一台机器时，请在“管理 → 网络/配对”里填当前有效的 6 位云端配对码，或直接粘贴服务端签发的 ses- 令牌。

联调自检
--------
仓库 tools/check_server.py 可验证服务器可达性、令牌有效性和事件上传：

  python tools/check_server.py --pair-code <有效6位码> --upload-test
  python tools/check_server.py --token ses-xxxxxxxx --upload-test

注意：配对码/令牌是敏感凭据，不要写进公开文档或提交。
