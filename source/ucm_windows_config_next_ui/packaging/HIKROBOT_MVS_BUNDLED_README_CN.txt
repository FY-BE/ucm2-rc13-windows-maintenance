本目录随 UCM2 R2S 安装包提供海康机器人 MVS 支持文件。

内容包括：
- Runtime\Win64_x64：64 位 MVS 运行库，供 UcmReferenceForceBridge.exe 使用；
- Development\Samples\Python\MvImport：相机 Python 接口模块；
- Drivers\GigE：GigE Vision 过滤驱动及官方安装脚本；
- Drivers\Usb3.0：USB3 Vision 驱动及官方安装脚本；
- Licenses：随本机 MVS 安装提供的许可声明。

使用方法：
1. 先关闭 MVS 客户端和所有正在使用相机的程序；
2. 右键运行“Install-Hikrobot-MVS-Drivers.cmd”，按 Windows 管理员提示确认；
3. 安装完成后重新插拔相机，或重启 Windows；
4. 再启动 UCM2 R2S 的标准力标定页面。

这个目录来自本机已安装的 MVS 发行文件，未包含独立 MVS 主安装器，也不会替代厂商许可。若 Windows 拒绝驱动安装，请使用与相机型号匹配的官方 MVS 安装包。
