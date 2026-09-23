# Windows ConfigStudio 项目维护规则

## 1. 定位与目标

本项目只维护 UCM2-RC13 Windows ConfigStudio，上位机与 ARM、STM32、HMI 分开管理。学习目标是吃透 CMake/Qt/USB/EtherCAT 入口、线程和界面调用链；维护目标是复现构建、定位问题、迭代功能并保留可回退版本。

## 2. 目录与源码入口

- `source/`：唯一源码编辑区，包含实际 Qt/C++ 工程。
- `build/`：可删除的临时构建目录。
- `artifacts/`：程序、依赖、安装包和发布候选。
- `evidence/`：构建、CTest、运行和 USB/现场验证证据。
- `docs/`：架构图、模块/线程地图、调用链、构建故障和维护教程。
- `tools/`：可复用构建/同步辅助脚本。

## 3. 基线与构建

- 当前交接基线：tag `handover-20260922-r1`。
- 推荐工具链：Qt 6.8 MSVC2022、CMake、Ninja、`E:\baize\deps\libusb`。
- 构建前记录源码 full commit、VS/Qt/CMake/Ninja 版本及依赖路径。
- 每次构建记录完整配置命令、退出码、产物路径和 SHA256。
- 构建成功不等于 UI、USB、EtherCAT 或现场设备验证成功。

## 4. 学习与迭代

`docs/` 至少维护一图读懂、CMake target 图、Qt 页面/信号槽路径、线程与资源所有权、USB/EtherCAT 数据流、关键函数说明、测试与故障定位手册。函数说明写清功能、输入、输出、线程语境、失败语义和可改点。

修改前先确认基线和差异，优先做最小可回退改动。源码改动、构建参数和临时证据分开提交；Qt/Windows 头文件、编译选项或依赖变更要记录原因和回退方式。离线测试不能冒充设备验证。

## 5. 验收与回退

分别标记源码审查、配置成功、编译成功、CTest、离线运行、USB/设备和现场验证。回退使用 tag/commit 与对应产物，不使用未绑定来源的 exe 覆盖 `artifacts/`。
