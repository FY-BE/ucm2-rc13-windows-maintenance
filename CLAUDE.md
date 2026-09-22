# Windows 项目维护规则

本项目只维护 Windows ConfigStudio。

- 只在 `source/` 修改；`build/` 是可删除的临时目录；`artifacts/` 放程序/依赖/安装包；`evidence/` 放构建、CTest 和运行验证证据。
- 初始快照含交付时未提交工作区内容，后续首次修改前必须先确认基线提交和差异清单。
- 构建使用 Qt 6.8 MSVC2022、CMake、Ninja 和 `E:\baize\deps\libusb`；缺环境先记录，不改源码绕过。
- 编译、CTest、运行截图和 USB/现场验证分别记录，不能混为一个“已验收”结论。
