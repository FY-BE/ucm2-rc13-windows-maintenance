# UCM2-RC13 Windows ConfigStudio 维护项目

这是独立的 Windows ConfigStudio 工程维护项目，不与 ARM、STM32、HMI 共用 Git 历史。

- 源码入口：`source/`
- 临时构建：`build/`
- 可发布程序和依赖：`artifacts/`
- 构建与测试证据：`evidence/`
- 预期工具链：Qt 6.8 MSVC2022、CMake、Ninja、libusb

接手快照含原工作区未提交内容；初始 tag 只表示“接手基线”，不表示 Windows 已编译验收。
